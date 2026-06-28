export module forge.plugins.db.rocksdb.types;

export import forge.rocksdb.types;

export namespace forge::plugins::db::rocksdb {

namespace backend = forge::rocksdb;

using status_code = backend::status_code;
using config = backend::config;
using read_options = backend::read_options;
using write_options = backend::write_options;
using family = backend::family;
using operation_kind = backend::operation_kind;
using entry = backend::entry;
using scan_request = backend::scan_request;
using scan_result = backend::scan_result;
using operation = backend::operation;

using backend::append_u8;
using backend::append_u32_be;
using backend::append_u64_be;
using backend::make_key;
using backend::make_u64_key;
using backend::read_u64_be;
using backend::to_bytes;
using backend::to_string;

} // namespace forge::plugins::db::rocksdb
