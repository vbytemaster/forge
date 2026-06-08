module;

#include <memory>
#include <utility>

export module fcl.api.handle;

export namespace fcl::api {

template <typename Interface> class handle {
 public:
   handle() = default;
   explicit handle(std::shared_ptr<Interface> local) : local_(std::move(local)) {}

   [[nodiscard]] explicit operator bool() const noexcept {
      return static_cast<bool>(local_);
   }

   [[nodiscard]] Interface* operator->() const noexcept {
      return local_.get();
   }

   [[nodiscard]] std::shared_ptr<Interface> shared() const noexcept {
      return local_;
   }

 private:
   std::shared_ptr<Interface> local_;
};

} // namespace fcl::api
