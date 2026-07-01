#pragma once

#define FORGE_OBJECTDB_OBJECT(object_index_type)                                                                        \
   namespace forge::objectdb {                                                                                         \
   template <>                                                                                                          \
   struct object_index_for_id<typename object_index_type::id_type> {                                                     \
      using type = object_index_type;                                                                                   \
   };                                                                                                                   \
   }
