import forge.log.appender;
import forge.log.console_appender;
import forge.log.logger_config;

namespace forge {

static bool reg_console_appender = log_config::register_appender<console_appender>("console");

} // namespace forge
