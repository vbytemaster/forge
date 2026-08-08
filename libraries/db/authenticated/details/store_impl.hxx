#pragma once

#include <memory>

namespace forge::db::authenticated {

struct store::impl {
   impl(std::shared_ptr<forge::db::core::driver> driver_value, config settings_value);

   std::shared_ptr<forge::db::core::driver> driver;
   config settings;
   digest namespace_hash;
};

} // namespace forge::db::authenticated
