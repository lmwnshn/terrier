#pragma once

#include <string>

#include "catalog/postgres/pg_language.h"
#include "common/managed_pointer.h"
#include "storage/projected_row.h"
#include "storage/storage_defs.h"

namespace noisepage::storage {
class RecoveryManager;
class SqlTable;

namespace index {
class Index;
}  // namespace index
}  // namespace noisepage::storage

namespace noisepage::transaction {
class TransactionContext;
}  // namespace noisepage::transaction

namespace noisepage::catalog::postgres {
class Builder;

/** The NoisePage version of pg_language. */
class PgLanguageImpl {
 public:
  /**
   * Prepare to create pg_language.
   * Does NOT create anything until the relevant bootstrap functions are called.
   *
   * @param db_oid          The OID of the database that pg_language should be created in.
   */
  explicit PgLanguageImpl(db_oid_t db_oid);

  /** Bootstrap the projected row initializers for pg_language. */
  void BootstrapPRIs();

  /**
   * Bootstrap:
   *    pg_language
   *    pg_languages_oid_index
   *    pg_languages_name_index
   *
   * @param txn             The transaction to bootstrap in.
   * @param dbc             The catalog object to bootstrap in.
   */
  void Bootstrap(common::ManagedPointer<transaction::TransactionContext> txn,
                 common::ManagedPointer<DatabaseCatalog> dbc);

  /**
   * Create a language entry in the pg_language table.
   *
   * @param txn             The transaction to use.
   * @param lanname         The name of the language to insert.
   * @param oid             The OID to assign to the language.
   * @return True if the creation succeeded. False otherwise.
   */
  bool CreateLanguage(const common::ManagedPointer<transaction::TransactionContext> txn, const std::string &lanname,
                      language_oid_t oid);

  /**
   * Look up a language entry in the pg_language table.
   *
   * @param txn             The transaction to use.
   * @param lanname         The name of the language to look up.
   * @return The OID of the language if found. Else INVALID_LANGUAGE_OID.
   */
  language_oid_t GetLanguageOid(common::ManagedPointer<transaction::TransactionContext> txn,
                                const std::string &lanname);

  /**
   * Delete a language entry from the pg_language table.
   *
   * @param txn             The transaction to use.
   * @param oid             The OID of the language to delete.
   * @return True if deletion is successful. False otherwise.
   */
  bool DropLanguage(common::ManagedPointer<transaction::TransactionContext> txn, language_oid_t oid);

 private:
  friend class Builder;
  friend class storage::RecoveryManager;

  /** Bootstrap all the builtin languages in pg_languages. */
  void BootstrapLanguages(common::ManagedPointer<transaction::TransactionContext> txn,
                          common::ManagedPointer<DatabaseCatalog> dbc);

  const db_oid_t db_oid_;

  /**
   * The table and indexes that define pg_language.
   * Created by: Builder::CreateDatabaseCatalog.
   * Cleaned up by: DatabaseCatalog::TearDown, where the scans from pg_class and pg_index pick these up.
   */
  ///@{
  storage::SqlTable *languages_;                 ///< The language table.
  storage::index::Index *languages_oid_index_;   ///< Indexed on: language OID
  storage::index::Index *languages_name_index_;  ///< Indexed on: language name, namespace
  ///@}

  storage::ProjectedRowInitializer pg_language_all_cols_pri_;
  storage::ProjectionMap pg_language_all_cols_prm_;
};

}  // namespace noisepage::catalog::postgres
