module;

#include <string_view>

export module forge.db.mdbx.driver:error;

namespace forge::db::mdbx::detail {

[[nodiscard]] bool mdbx_success(int code) noexcept;
[[nodiscard]] bool mdbx_not_found(int code) noexcept;
void require_mdbx_success(int code, std::string_view operation);

} // namespace forge::db::mdbx::detail
