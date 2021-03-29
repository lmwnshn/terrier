#include "replication/primary_replication_manager.h"

#include "loggers/replication_logger.h"
#include "replication/replication_messages.h"

namespace noisepage::replication {

PrimaryReplicationManager::PrimaryReplicationManager(
    common::ManagedPointer<messenger::Messenger> messenger, const std::string &network_identity, uint16_t port,
    const std::string &replication_hosts_path,
    common::ManagedPointer<common::ConcurrentBlockingQueue<storage::BufferedLogWriter *>> empty_buffer_queue)
    : ReplicationManager(messenger, network_identity, port, replication_hosts_path, empty_buffer_queue) {}

PrimaryReplicationManager::~PrimaryReplicationManager() = default;

void PrimaryReplicationManager::EventLoop(common::ManagedPointer<messenger::Messenger> messenger,
                                          const messenger::ZmqMessage &zmq_msg,
                                          common::ManagedPointer<BaseReplicationMessage> msg) {
  switch (msg->GetMessageType()) {
    case ReplicationMessageType::TXN_APPLIED: {
      Handle(zmq_msg, *msg.CastManagedPointerTo<TxnAppliedMsg>());
      break;
    }
    default: {
      // Delegate to the common ReplicationManager event loop.
      ReplicationManager::EventLoop(messenger, zmq_msg, msg);
      break;
    }
  }
}

void PrimaryReplicationManager::ReplicateBatchOfRecords(storage::BufferedLogWriter *records_batch,
                                                        const std::vector<storage::CommitCallback> &commit_callbacks,
                                                        const transaction::ReplicationPolicy &policy) {
  NOISEPAGE_ASSERT(policy != transaction::ReplicationPolicy::DISABLE, "Replication is disabled, so why are we here?");

  // Unfortunately, read-only transactions do not generate log records.
  // Therefore there may be nothing to replicate, but the commit callbacks still have to be invoked.
  bool has_records = records_batch != nullptr;

  // TODO(WAN): DEBUG BLOCK
  std::vector<transaction::timestamp_t> ts;
  {
    for (auto &cb : commit_callbacks) {
      ts.emplace_back(cb.txn_start_time_);
    }
  }

  if (policy == transaction::ReplicationPolicy::ASYNC || !has_records) {
    // In asynchronous replication, just invoke the commit callbacks immediately.
    for (const auto &cb : commit_callbacks) {
      cb.fn_(cb.arg_);
    }
  } else {
    // Copy the commit callbacks into our local list. The callbacks are invoked when the replicas notify the primary
    // that the replicas have applied their corresponding transactions.
    {
      std::unique_lock lock(callbacks_mutex_);
      txn_callbacks_.emplace(BatchOfCommitCallbacks{commit_callbacks, has_records});
    }
  }

  if (has_records) {
    // Send the batch of records to all replicas.
    ReplicationMessageMetadata metadata(GetNextMessageId());
    RecordsBatchMsg msg(metadata, GetNextBatchId(), records_batch);
    REPLICATION_LOG_TRACE(fmt::format("BATCH {} TXNS {}", msg.GetBatchId(), common::json(ts).dump()));

    messenger::messenger_cb_id_t destination_cb =
        messenger::Messenger::GetBuiltinCallback(messenger::Messenger::BuiltinCallback::NOOP);
    for (const auto &replica : replicas_) {
      Send(replica.first, msg, messenger::CallbackFns::Noop, destination_cb, true);
    }

    // Return the buffered log writer to the pool if necessary.
    if (records_batch->MarkSerialized()) {
      empty_buffer_queue_->Enqueue(records_batch);
    }
  }
}

record_batch_id_t PrimaryReplicationManager::GetNextBatchId() {
  record_batch_id_t next_batch_id = next_batch_id_++;
  if (next_batch_id_.load() == INVALID_RECORD_BATCH_ID) {
    next_batch_id_++;
  }
  return next_batch_id;
}

void PrimaryReplicationManager::Handle(const messenger::ZmqMessage &zmq_msg, const TxnAppliedMsg &msg) {
  REPLICATION_LOG_TRACE(fmt::format("[RECV] TxnAppliedMsg from {}: ID {} TXN {}", zmq_msg.GetRoutingId(),
                                    msg.GetMessageId(), msg.GetAppliedTxnId()));
  // Acknowledge receipt of the txn having been applied on the replica.
  SendAckForMessage(zmq_msg, msg);
  // Mark the transaction as applied by the specific replica.
  transaction::timestamp_t txn_id = msg.GetAppliedTxnId();
  {
    std::unique_lock lock(callbacks_mutex_);
    if (txns_applied_on_replicas_.find(txn_id) == txns_applied_on_replicas_.end()) {
      txns_applied_on_replicas_.emplace(txn_id, std::unordered_set<std::string>{});
    }
    std::unordered_set<std::string> &replicas = txns_applied_on_replicas_.at(txn_id);
    replicas.emplace(zmq_msg.GetRoutingId());
    // Check if all the replicas have applied this transaction.
    if (replicas.size() == replicas_.size()) {
      // If so, there may be new transaction callbacks that we can invoke.
      ProcessTxnCallbacks();
    }
  }
}

void PrimaryReplicationManager::ProcessTxnCallbacks() {
  // TODO(WAN): In theory, is async just "go ahead and process these without checking"?
  // TODO(WAN): Do NOT reuse this function without checking for races.

  while (!txn_callbacks_.empty()) {
    BatchOfCommitCallbacks &item = txn_callbacks_.front();

    if (!item.has_records_) {
      // If there are no records, just execute all of the callbacks.
      for (const auto &callback : item.callbacks_) {
        callback.fn_(callback.arg_);
      }
    } else {
      // Otherwise, check that each respective callback's transaction has been applied on all the replicas.
      std::vector<storage::CommitCallback> &callbacks = item.callbacks_;
      for (auto vec_it = callbacks.begin(); vec_it != callbacks.end();) {
        storage::CommitCallback &callback = *vec_it;
        // Check if all the replicas have applied the transaction.
        bool all_replicas_applied =
            (txns_applied_on_replicas_.find(callback.txn_start_time_) != txns_applied_on_replicas_.end()) &&
            (txns_applied_on_replicas_.at(callback.txn_start_time_).size() == replicas_.size());
        if (!all_replicas_applied) {
          return;
        }
        // If all replicas have applied the transaction, then invoke the callback and erase the callback.
        callback.fn_(callback.arg_);
        txns_applied_on_replicas_.erase(callback.txn_start_time_);
        REPLICATION_LOG_TRACE(fmt::format("Commit callback invoked for txn: {}", callback.txn_start_time_));
        vec_it = callbacks.erase(vec_it);
      }
    }
    // If all the callbacks in one batch have been exhausted, erase the exhausted batch.
    txn_callbacks_.pop();
  }
}

}  // namespace noisepage::replication