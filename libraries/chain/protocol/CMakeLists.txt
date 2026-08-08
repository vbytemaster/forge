add_library(
   forge_chain_protocol STATIC
   values.cpp
   time.cpp
   types.cpp
   action.cpp
   action_receipt.cpp
   transaction.cpp
   block.cpp
   abi.cpp
   authority.cpp
   system.cpp
   producer_schedule.cpp
   producer_authority.cpp
   finalizer_authority.cpp
   finalizer_policy.cpp
   state_query.cpp
)
forge_target_modules_at(forge_chain_protocol chain/protocol)

target_include_directories(forge_chain_protocol PUBLIC ${FORGE_PUBLIC_INCLUDE_DIRS})

target_link_libraries(
   forge_chain_protocol PUBLIC
      forge_chain_core
      forge_codec_hex
      forge_compression
      forge_raw
      forge_variant
      forge_crypto_asymmetric_values
      forge_crypto_asymmetric
      forge_crypto_digest
)
forge_target_contract_guest_component(forge_chain_protocol ID forge.chain.protocol)
