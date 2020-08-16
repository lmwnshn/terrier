#include "parser/transaction_statement.h"

#include "binder/sql_node_visitor.h"

namespace terrier::parser {
void TransactionStatement::Accept(common::ManagedPointer<binder::SqlNodeVisitor> v) {
  v->Visit(common::ManagedPointer(this));
}
}  // namespace terrier::parser