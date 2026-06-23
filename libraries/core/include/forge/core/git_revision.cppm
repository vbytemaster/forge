module;
#include <stdint.h>

export module forge.core.git_revision;

export namespace forge {

extern const char* const git_revision_sha;
extern const uint32_t git_revision_unix_timestamp;

} // end namespace forge
