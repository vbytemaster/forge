#!/usr/bin/env python3

from __future__ import annotations

import re
import sys
from collections import defaultdict
from pathlib import Path


SOURCE_SUFFIXES = {".cpp", ".cppm", ".hpp", ".hxx"}
LAYOUT_ROOTS = ("libraries", "plugins", "guest/libraries")
SCAN_ROOTS = ("libraries", "plugins", "guest/libraries", "tests")
EXCLUDED_PARTS = {".git", "legacy", "vendor", "__pycache__"}
MODULE_NAME = r"forge(?:\.[A-Za-z_][A-Za-z0-9_]*)+(?::[A-Za-z_][A-Za-z0-9_]*)?"
MODULE_DECLARATION = re.compile(rf"^\s*export\s+module\s+({MODULE_NAME})\s*;")
MODULE_UNIT = re.compile(rf"^\s*(?:export\s+)?module\s+({MODULE_NAME})\s*;")
MODULE_IMPORT = re.compile(rf"^\s*(?:export\s+)?import\s+({MODULE_NAME}|:[A-Za-z_][A-Za-z0-9_]*)\s*;")
INCLUDE = re.compile(r'^\s*#\s*include\s*([<"][^>"]+[>"])')
BROAD_EXPORT = re.compile(r"^\s*export\s*\{")
CONDITIONAL_START = re.compile(r"^\s*#\s*(?:if|ifdef|ifndef)\b")
CONDITIONAL_BRANCH = re.compile(r"^\s*#\s*(?:elif|else)\b")
CONDITIONAL_END = re.compile(r"^\s*#\s*endif\b")
PRIVATE_DECLARATION = re.compile(r"^(?:class|struct|enum(?:\s+class)?)\s+([A-Za-z_][A-Za-z0-9_:]*)")
VM_WASM_EXPORT = re.compile(r"\bFORGE_VM_WASM_EXPORT\b")
UNQUALIFIED_C_MEMORY = re.compile(r"(?<![:\w])(?:memcpy|memmove|memset|memcmp)\s*\(")


def source_files(root: Path, roots: tuple[str, ...]) -> list[Path]:
   files: list[Path] = []
   for name in roots:
      base = root / name
      if not base.exists():
         continue
      for path in base.rglob("*"):
         if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
            continue
         relative = path.relative_to(root)
         if any(part in EXCLUDED_PARTS or part.startswith("build-") for part in relative.parts):
            continue
         files.append(path)
   return sorted(files)


def check_layout(root: Path, errors: list[str]) -> None:
   for path in source_files(root, LAYOUT_ROOTS):
      relative = path.relative_to(root)
      parts = relative.parts
      if path.suffix == ".hxx" and "details" not in parts:
         errors.append(f"{relative}: private .hxx must live under details/")
      if path.suffix == ".hpp" and "details" in parts:
         errors.append(f"{relative}: details/ headers must use .hxx")
      if path.suffix == ".cppm" and "include" not in parts:
         errors.append(f"{relative}: public .cppm must live under include/")
      if path.suffix == ".cpp" and ("include" in parts or "details" in parts):
         errors.append(f"{relative}: implementation .cpp must live at the library/plugin root")


def check_aggregates(root: Path, errors: list[str]) -> None:
   plugin_source = root / "plugins" / "plugins.cpp"
   if plugin_source.exists():
      errors.append("plugins/plugins.cpp: code-less plugin aggregate must not own a source")

   cmake = (root / "plugins" / "CMakeLists.txt").read_text()
   if not re.search(r"add_library\s*\(\s*forge_plugins\s+INTERFACE\s*\)", cmake):
      errors.append("plugins/CMakeLists.txt: forge_plugins must be an INTERFACE target")

   anchor = re.compile(r"\b(?:aggregate|dummy)_anchor\b")
   for path in source_files(root, ("libraries", "plugins", "tests")):
      for line_number, line in enumerate(path.read_text(errors="ignore").splitlines(), 1):
         if anchor.search(line):
            errors.append(f"{path.relative_to(root)}:{line_number}: dummy anchor symbol is forbidden")

   comments = re.compile(r"//[^\n]*|/\*.*?\*/", re.DOTALL)
   module_line = re.compile(rf"^\s*(?:export\s+)?module(?:\s+{MODULE_NAME})?\s*;\s*$", re.MULTILINE)
   import_line = re.compile(rf"^\s*(?:export\s+)?import\s+(?:{MODULE_NAME}|:[A-Za-z_]\w*)\s*;\s*$", re.MULTILINE)
   include_line = re.compile(r"^\s*#\s*include\s*[<\"][^>\"]+[>\"]\s*$", re.MULTILINE)
   for path in source_files(root, LAYOUT_ROOTS):
      if path.suffix != ".cppm":
         continue
      source = comments.sub("", path.read_text(errors="ignore"))
      if import_line.search(source) is None:
         continue
      remainder = include_line.sub("", import_line.sub("", module_line.sub("", source))).strip()
      if not remainder:
         errors.append(f"{path.relative_to(root)}: aggregate-only module is forbidden")


def component_roots(root: Path) -> list[Path]:
   roots: list[Path] = []
   for top in LAYOUT_ROOTS:
      for cmake in (root / top).rglob("CMakeLists.txt"):
         component = cmake.parent
         if any(component.glob("*.cpp")):
            roots.append(component)
   return sorted(roots)


def matching_headers(component: Path, stem: str) -> list[Path]:
   matches: list[Path] = []
   include = component / "include"
   if include.exists():
      matches.extend(include.rglob(f"{stem}.cppm"))
   private = component / "details" / f"{stem}.hxx"
   if private.exists():
      matches.append(private)
   return sorted(matches)


def check_pairing(root: Path, errors: list[str]) -> None:
   for component in component_roots(root):
      sources = {path.stem: path for path in component.glob("*.cpp")}
      headers = {stem: matching_headers(component, stem) for stem in sources}

      for stem, source in sorted(sources.items()):
         relative = source.relative_to(root)
         direct = headers[stem]
         if len(direct) == 1:
            continue
         if len(direct) > 1:
            owners = ", ".join(str(path.relative_to(root)) for path in direct)
            errors.append(f"{relative}: implementation has multiple exact owners: {owners}")
            continue

         aspect_owners = [
            owner
            for owner in sources
            if stem.startswith(f"{owner}_") and len(headers[owner]) == 1
         ]
         if aspect_owners:
            continue
         errors.append(
            f"{relative}: implementation needs an exact {stem}.cppm/{stem}.hxx owner "
            "or a paired X.cpp for an X_<aspect>.cpp source"
         )


def check_macro_only_header(root: Path, path: Path, errors: list[str]) -> None:
   text = re.sub(r"/\*.*?\*/", "", path.read_text(errors="ignore"), flags=re.DOTALL)
   in_macro = False

   for line_number, line in enumerate(text.splitlines(), 1):
      stripped = re.sub(r"//.*$", "", line).strip()
      if in_macro:
         in_macro = line.rstrip().endswith("\\")
         continue
      if not stripped:
         continue
      if stripped.startswith("#"):
         if re.match(r"#\s*define\b", stripped):
            in_macro = line.rstrip().endswith("\\")
         continue
      errors.append(
         f"{path.relative_to(root)}:{line_number}: macro-only public header contains a C++ declaration"
      )


def check_vm_wasm_boundaries(root: Path, errors: list[str]) -> None:
   component = root / "libraries" / "vm" / "wasm"
   if not component.exists():
      return

   details = component / "details"
   if details.exists():
      errors.append(f"{details.relative_to(root)}: vm_wasm must not install or compile private source headers")

   include = component / "include" / "forge" / "vm" / "wasm"
   allowed_headers = {"host_function.hpp", "opcode_macros.hpp"}
   headers = {path.name for path in include.glob("*.hpp")}
   unexpected = headers - allowed_headers
   if unexpected:
      errors.append(f"{include.relative_to(root)}: unexpected public headers: {', '.join(sorted(unexpected))}")

   for name in sorted(allowed_headers):
      path = include / name
      if not path.exists():
         errors.append(f"{path.relative_to(root)}: required macro-only public header is missing")
         continue
      check_macro_only_header(root, path, errors)

   for path in sorted(include.glob("*.cppm")):
      relative = path.relative_to(root)
      for line_number, line in enumerate(path.read_text(errors="ignore").splitlines(), 1):
         included = INCLUDE.match(line)
         if included and (".hxx" in included.group(1) or "details/" in included.group(1)):
            errors.append(f"{relative}:{line_number}: public VM module includes a private source header")
         if included and "forge/vm/wasm/" in included.group(1) and included.group(1) not in {
            "<forge/vm/wasm/host_function.hpp>",
            "<forge/vm/wasm/opcode_macros.hpp>",
         }:
            errors.append(f"{relative}:{line_number}: VM components must use module imports")
         if VM_WASM_EXPORT.search(line):
            errors.append(f"{relative}:{line_number}: FORGE_VM_WASM_EXPORT is forbidden")
         if UNQUALIFIED_C_MEMORY.search(line):
            errors.append(f"{relative}:{line_number}: VM modules must qualify C memory functions through std")


def check_plugin_impl_ownership(root: Path, errors: list[str]) -> None:
   for path in sorted((root / "plugins").rglob("details/plugin_impl.hxx")):
      for line_number, line in enumerate(path.read_text(errors="ignore").splitlines(), 1):
         declaration = PRIVATE_DECLARATION.match(line)
         if declaration and declaration.group(1) != "plugin::impl":
            errors.append(
               f"{path.relative_to(root)}:{line_number}: plugin_impl.hxx may only own plugin::impl; "
               f"move {declaration.group(1)} to its exact private header"
            )


def check_chain_savanna_boundaries(root: Path, errors: list[str]) -> None:
   component = root / "libraries" / "chain" / "savanna"
   if not component.exists():
      return

   forbidden = {
      "forge.chain.protocol": "protocol modules",
      "forge::chain::protocol": "protocol namespace",
      "bls12-381": "private BLS backend",
      "bls12_381": "private BLS backend",
      "blockchain::": "product namespace",
      "storlane::": "product namespace",
      "eosio::": "donor namespace",
      "spring::": "donor namespace",
   }
   for path in source_files(root, ("libraries/chain/savanna",)):
      relative = path.relative_to(root)
      source = path.read_text(errors="ignore")
      for token, owner in forbidden.items():
         if token in source:
            errors.append(f"{relative}: Savanna kernel must not depend on {owner} ({token})")


def check_chain_api_shape(root: Path, errors: list[str]) -> None:
   component = root / "libraries" / "chain" / "api"
   if not component.exists():
      return

   include = component / "include" / "forge" / "chain" / "api"
   nested_modules = sorted(path for path in include.rglob("*.cppm") if path.parent != include)
   if nested_modules:
      rendered = ", ".join(str(path.relative_to(root)) for path in nested_modules)
      errors.append("chain API modules must use a flat public include layout: " + rendered)

   forbidden_directories = {
      "types",
      "info",
      "block",
      "state",
      "transaction",
      "admin",
      "client",
   }
   nested_components = sorted(
      path.name
      for path in component.iterdir()
      if path.is_dir() and path.name in forbidden_directories
   )
   if nested_components:
      errors.append("chain API must be one flat library, found nested components: " + ", ".join(nested_components))

   cmake = (component / "CMakeLists.txt").read_text()
   if not re.search(r"add_library\s*\(\s*forge_chain_api\s+STATIC\b", cmake):
      errors.append("libraries/chain/api/CMakeLists.txt: expected one compiled forge_chain_api target")
   if re.search(r"\bforge_chain_api_(?:types|info|block|state|transaction|admin|client)\b", cmake):
      errors.append("libraries/chain/api/CMakeLists.txt: split chain API targets are forbidden")

   nested_api_namespace = re.compile(r"\bnamespace\s+forge::chain::api::(?:info|block|state|transaction|admin)\b")
   wire_record = re.compile(
      r"^\s*(?:export\s+)?(?:struct|enum\s+class)\s+\w*(?:request|result|response)\b",
      re.MULTILINE,
   )
   for path in sorted(include.glob("*.cppm")):
      source = path.read_text(errors="ignore")
      if nested_api_namespace.search(source):
         errors.append(f"{path.relative_to(root)}: API names must be classes in forge::chain::api")
      if "BOOST_DESCRIBE" in source or wire_record.search(source):
         errors.append(f"{path.relative_to(root)}: chain API wire records belong to forge.chain.protocol")

   protocol_include = root / "libraries" / "chain" / "protocol" / "include" / "forge" / "chain" / "protocol"
   get_dto = re.compile(r"^\s*(?:export\s+)?(?:struct|class|using)\s+get_\w+", re.MULTILINE)
   query_modules = ("audit.cppm", "info.cppm", "block_query.cppm", "state_query.cppm",
                    "transaction_query.cppm", "admin.cppm")
   for name in query_modules:
      path = protocol_include / name
      if not path.exists():
         errors.append(f"{path.relative_to(root)}: missing chain protocol query module")
         continue
      source = path.read_text(errors="ignore")
      if get_dto.search(source):
         errors.append(f"{path.relative_to(root)}: DTO names must not repeat the get operation")
      if re.search(r"\busing\s+\w+_response\s*=", source):
         errors.append(f"{path.relative_to(root)}: response DTOs must be concrete records, not aliases")
   if sorted(protocol_include.glob("api_*.cppm")):
      errors.append("chain protocol query modules must not use the api_* filename prefix")


def check_contract_sdk_workflow(root: Path, errors: list[str]) -> None:
   path = root / ".github" / "workflows" / "contract-sdk.yml"
   if not path.exists():
      return

   source = path.read_text(errors="ignore")
   if "  pull_request:\n" in source:
      pull_request = source.split("  pull_request:\n", 1)[1].split("  push:\n", 1)[0]
      for required in (
         '      - "CMakeLists.txt"',
         '      - "cmake/**"',
         '      - "libraries/asio/**"',
         '      - "libraries/chain/core/**"',
         '      - "libraries/chain/protocol/**"',
         '      - "libraries/codec/json/**"',
         '      - "libraries/compression/**"',
         '      - "libraries/config/core/**"',
         '      - "libraries/core/**"',
         '      - "libraries/crypto/**"',
         '      - "libraries/db/**"',
         '      - "libraries/exceptions/**"',
         '      - "libraries/db/ids/**"',
         '      - "libraries/raw/**"',
         '      - "libraries/reflect/**"',
         '      - "libraries/schema/**"',
         '      - "libraries/variant/**"',
         '      - "libraries/vm/wasm/**"',
         '      - "libraries/contract/**"',
         '      - "guest/**"',
         '      - "tools/**"',
         '      - "vendor/**"',
      ):
         if required not in pull_request:
            errors.append(
               f"{path.relative_to(root)}: pull_request paths must include {required.strip()[2:]}"
            )
   elif "  workflow_dispatch:\n" not in source:
      errors.append(f"{path.relative_to(root)}: workflow must define pull_request or workflow_dispatch")

   sysroot_cache_inputs = "hashFiles('guest/sysroot/build.sh', 'guest/sysroot/include/**')"
   if sysroot_cache_inputs not in source:
      errors.append(
         f"{path.relative_to(root)}: contract sysroot cache key must hash its build script and headers"
      )

   macos_sdkroot = 'echo "SDKROOT=$(xcrun --sdk macosx --show-sdk-path)" >> "$GITHUB_ENV"'
   if source.count(macos_sdkroot) < 2:
      errors.append(
         f"{path.relative_to(root)}: macOS developer and release jobs must export the selected SDKROOT"
      )

   recovery_contract = "-DFORGE_CONTRACT_TEST_RECOVERY_WASM="
   if source.count(recovery_contract) != 2:
      errors.append(
         f"{path.relative_to(root)}: developer and release E2E jobs must execute the recovery contract"
      )

   for incompatible_flag in ('CXXFLAGS=-stdlib=libc++', 'LDFLAGS=-stdlib=libc++'):
      if incompatible_flag in source:
         errors.append(
            f"{path.relative_to(root)}: Linux host tooling must not override its packaged C++ ABI; "
            f"remove {incompatible_flag}"
         )

   for required in (
      "ppa:ubuntu-toolchain-r/test",
      "g++-15",
      "FORGE_CONTRACT_LLVM_SOURCE_DIR",
      "--target forge_contract_llvm -j 4",
   ):
      if required not in source:
         errors.append(
            f"{path.relative_to(root)}: Contract SDK workflow is missing {required}"
         )

   release_build = re.search(
      r"cmake --build build/contract-release-consumer\s+\\\n"
      r"\s+--target (?P<targets>(?:[^\n]|\\\n)+?)\s+\\\n"
      r"\s+-j 4",
      source,
   )
   required_release_contracts = {"recordtest", "legacynotify", "recovery"}
   release_targets = set() if release_build is None else set(release_build.group("targets").split())
   missing_release_contracts = sorted(required_release_contracts - release_targets)
   if missing_release_contracts:
      errors.append(
         f"{path.relative_to(root)}: release consumer must build E2E contracts before configuration: "
         f"{', '.join(missing_release_contracts)}"
      )


def check_chain_audited_api_workflow(root: Path, errors: list[str]) -> None:
   path = root / ".github" / "workflows" / "chain-audited-api.yml"
   if not path.exists():
      return

   source = path.read_text(errors="ignore")
   required_developer_dir = (
      "FORGE_MACOS_DEVELOPER_DIR: /Applications/Xcode_26.3.app/Contents/Developer"
   )
   if required_developer_dir not in source:
      errors.append(
         f"{path.relative_to(root)}: macOS acceptance must pin the Xcode 26.3 developer directory"
      )

   sdkroot_export = 'echo "SDKROOT=$(xcrun --sdk macosx --show-sdk-path)" >> "$GITHUB_ENV"'
   if source.count(sdkroot_export) != 2:
      errors.append(
         f"{path.relative_to(root)}: native and performance jobs must export the selected macOS SDKROOT"
      )

   osx_sysroot = 'osx_options+=("-DCMAKE_OSX_SYSROOT=$SDKROOT")'
   if source.count(osx_sysroot) != 2:
      errors.append(
         f"{path.relative_to(root)}: native and performance configure steps must use the selected macOS SDKROOT"
      )

   isolated_glaze_prefix = 'CMAKE_PREFIX_PATH=$RUNNER_TEMP/forge-glaze;'
   if isolated_glaze_prefix in source:
      errors.append(
         f"{path.relative_to(root)}: isolated Glaze prefix must not enter CMAKE_PREFIX_PATH"
      )

   exact_glaze_config = 'glaze_config="$RUNNER_TEMP/forge-glaze/share/glaze/glazeConfig.cmake"'
   resolved_glaze_dir = 'echo "FORGE_GLAZE_DIR=$(cd "$(dirname "$glaze_config")" && pwd -P)"'
   explicit_glaze_dir = '-Dglaze_DIR="$FORGE_GLAZE_DIR"'
   shared_dependency_prefixes = (
      'CMAKE_PREFIX_PATH=$(brew --prefix);$(brew --prefix boost);'
      '$(brew --prefix libngtcp2);$(brew --prefix openssl@3)'
   )
   if (
      source.count(exact_glaze_config) != 3
      or source.count(resolved_glaze_dir) != 3
      or source.count(explicit_glaze_dir) != 4
      or source.count(shared_dependency_prefixes) != 3
   ):
      errors.append(
         f"{path.relative_to(root)}: every configure lane must isolate Glaze and preserve shared dependency prefixes"
      )

   for baseline, upper_bytes in (("1m", "8589934592"), ("10m", "68719476736")):
      invocation = re.compile(
         rf"--baseline {baseline}\s+--mdbx-upper-bytes {upper_bytes}\s+\\\s+--machine-label"
      )
      if invocation.search(source) is None:
         errors.append(
            f"{path.relative_to(root)}: {baseline} performance baseline must use its measured MDBX upper size"
         )

   try:
      native_acceptance = source.split("      - name: Build acceptance targets\n", 1)[1].split(
         "      - name: Run acceptance\n", 1
      )
      build_acceptance = native_acceptance[0]
      run_acceptance = native_acceptance[1].split("\n  sanitizer:\n", 1)[0]
   except IndexError:
      errors.append(f"{path.relative_to(root)}: cannot locate native acceptance steps")
      return

   for required_target in ("test_forge_package_chain_api_component", "test_forge_package_db_mdbx_component"):
      if required_target not in build_acceptance:
         errors.append(f"{path.relative_to(root)}: acceptance build is missing {required_target}")

   for required_test in (
      "test_forge_structure",
      "test_forge_vendor_compile_policy",
      "test_forge_vendor_compile_policy_multi_config",
      "test_forge_package_chain_api_component",
      "test_forge_package_db_mdbx_component",
      "test_forge_package_explicit_glaze_dir",
   ):
      if required_test not in run_acceptance:
         errors.append(f"{path.relative_to(root)}: acceptance test run is missing {required_test}")


def check_mdbx_module_boundary(root: Path, errors: list[str]) -> None:
   component = root / "libraries" / "db" / "mdbx"
   if not component.exists():
      return

   legacy_header = component / "details" / "error.hxx"
   if legacy_header.exists():
      errors.append(
         f"{legacy_header.relative_to(root)}: MDBX error declarations must use a private module partition"
      )

   partition = component / "include" / "forge" / "db" / "mdbx" / "error.cppm"
   if not partition.exists():
      errors.append(f"{partition.relative_to(root)}: MDBX error module partition is missing")
      return

   source = partition.read_text(errors="ignore")
   declaration = "export module forge.db.mdbx.driver:error;"
   if declaration not in source:
      errors.append(f"{partition.relative_to(root)}: expected private partition {declaration}")
   include_position = source.find("#include <string_view>")
   declaration_position = source.find(declaration)
   if include_position < 0 or declaration_position < 0 or include_position > declaration_position:
      errors.append(
         f"{partition.relative_to(root)}: string_view must be included in the global module fragment"
      )

   for implementation in sorted(component.glob("*.cpp")):
      implementation_source = implementation.read_text(errors="ignore")
      if "require_mdbx_success(" in implementation_source and "import :error;" not in implementation_source:
         errors.append(
            f"{implementation.relative_to(root)}: MDBX error helpers must come from the private module partition"
         )


def check_contract_sdk_components(root: Path, errors: list[str]) -> None:
   path = root / "guest" / "CMakeLists.txt"
   if not path.exists():
      return

   contract_include = root / "guest" / "libraries" / "contract" / "include" / "forge" / "contract"
   nested_modules = sorted(
      path for path in contract_include.rglob("*.cppm") if path.parent != contract_include
   )
   if nested_modules:
      rendered = ", ".join(str(path.relative_to(root)) for path in nested_modules)
      errors.append("guest contract modules must use a flat public include layout: " + rendered)

   source_c_headers = sorted(contract_include.glob("*.h"))
   if source_c_headers:
      rendered = ", ".join(str(header.relative_to(root)) for header in source_c_headers)
      errors.append(
         "generated Contract SDK C ABI headers must live outside library source include: " + rendered
      )

   eosio_include = root / "guest" / "libraries" / "eosio" / "include" / "eosio"
   for header in sorted(eosio_include.glob("*.hpp")):
      source = header.read_text(errors="ignore")
      if "boost/pfr" in source or "boost::pfr" in source:
         errors.append(
            f"{header.relative_to(root)}: EOSIO veneer must delegate aggregate serialization to forge.raw"
         )

   types_template = root / "guest" / "cmake" / "types.h.in"
   if not types_template.exists():
      errors.append("guest/cmake/types.h.in: generated Contract SDK C ABI types template is missing")

   source = path.read_text(errors="ignore")
   for required in (
      "-DCMAKE_C_FLAGS=${_forge_contract_llvm_path_map_flags}",
      "-DCMAKE_CXX_FLAGS=${_forge_contract_llvm_path_map_flags}",
      "-DCMAKE_C_FLAGS=${_forge_contract_wasm_path_map_flags}",
      "-DCMAKE_CXX_FLAGS=${_forge_contract_wasm_path_map_flags}",
      "-DCMAKE_ASM_FLAGS=${_forge_contract_wasm_path_map_flags}",
      "-DCMAKE_C_FLAGS=${_forge_contract_path_map_flags}",
      "-DCMAKE_CXX_FLAGS=${_forge_contract_path_map_flags}",
   ):
      if required not in source:
         errors.append(
            f"{path.relative_to(root)}: release SDK sub-builds must preserve path mapping: {required}"
         )

   libraries_cmake = (
      root / "guest" / "cmake" / "ForgeContractLibraries.cmake"
   ).read_text(errors="ignore")
   for required in (
      '"-ffile-prefix-map=${_product_source_root}=./source"',
      '"-fdebug-prefix-map=${_product_source_root}=./source"',
      '"-ffile-prefix-map=${CMAKE_BINARY_DIR}=./build"',
      '"-fdebug-prefix-map=${CMAKE_BINARY_DIR}=./build"',
      "_forge_contract_freeze_guest_target(",
      "FORGE_CONTRACT_SOURCE_ROOT",
   ):
      if required not in libraries_cmake:
         errors.append(
            "guest/cmake/ForgeContractLibraries.cmake: guest targets must share "
            f"project-wide path mapping: {required}"
         )
   for forbidden in (
      '"-ffile-prefix-map=${_source_dir}=./source"',
      '"-fdebug-prefix-map=${_source_dir}=./source"',
      '"-ffile-prefix-map=${_binary_dir}=./build"',
      '"-fdebug-prefix-map=${_binary_dir}=./build"',
   ):
      if forbidden in libraries_cmake:
         errors.append(
            "guest/cmake/ForgeContractLibraries.cmake: target-local path mapping "
            f"creates incompatible CMake 4.4 module variants: {forbidden}"
         )

   root_cmake = (root / "CMakeLists.txt").read_text(errors="ignore")
   for required in (
      '"$<BUILD_INTERFACE:-ffile-prefix-map=${_forge_contract_host_source_root}=.>"',
      '"$<BUILD_INTERFACE:-fdebug-prefix-map=${_forge_contract_host_source_root}=.>"',
      '"$<BUILD_INTERFACE:-ffile-prefix-map=${CMAKE_BINARY_DIR}=./build>"',
      '"$<BUILD_INTERFACE:-fdebug-prefix-map=${CMAKE_BINARY_DIR}=./build>"',
   ):
      if required not in root_cmake:
         errors.append(
            f"CMakeLists.txt: contract-sdk-host must map source and build paths: {required}"
         )

   tools_project = re.search(
      r"ExternalProject_Add\(\s*forge_contract_tools(?P<body>.*?)\n\s*\)", source, re.DOTALL
   )
   if (
      tools_project is None
      or "-DCMAKE_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}" not in tools_project.group("body")
   ):
      errors.append(
         f"{path.relative_to(root)}: release tools must inherit the SDK install libdir"
      )

   try:
      developer_profile = source.split(
         'else()\n   find_package(Clang 22.1 CONFIG REQUIRED)', 1
      )[1].split(
         'endif()\n\nset(_forge_contract_input_sysroot', 1
      )[0]
   except IndexError:
      errors.append(f"{path.relative_to(root)}: cannot locate developer Contract SDK profile")
      return

   for component in (
      "contract_abi",
      "contract_attributes",
      "contract_validation",
      "contract_manifest",
   ):
      if developer_profile.count(component) != 2:
         errors.append(
            f"{path.relative_to(root)}: developer Contract SDK must request {component} "
            "with and without an explicit Forge_DIR"
         )


def check_eosio_veneer(root: Path, errors: list[str]) -> None:
   path = root / "guest" / "libraries" / "eosio" / "include" / "eosio" / "dispatcher.hpp"
   if not path.exists():
      return

   source = path.read_text(errors="ignore")
   for forbidden in ("switch (action)", "execute_action<"):
      if forbidden in source:
         errors.append(f"{path.relative_to(root)}: EOSIO veneer must not own dispatcher algorithms")
   if "::forge::contract::dispatch(" not in source:
      errors.append(f"{path.relative_to(root)}: EOSIO dispatcher must delegate to forge.contract.dispatcher")

   generator = root / "libraries" / "contract" / "abi" / "generator.cpp"
   generated_source = generator.read_text(errors="ignore")
   for forbidden in ('output << "   switch (action)',):
      if forbidden in generated_source:
         errors.append(
            f"{generator.relative_to(root)}: generated dispatcher must delegate to forge.contract.dispatcher"
         )
   if 'forge::contract::dispatch(name{receiver}' not in generated_source:
      errors.append(
         f"{generator.relative_to(root)}: generated dispatcher does not delegate to forge.contract.dispatcher"
      )

   asset = root / "guest" / "libraries" / "eosio" / "include" / "eosio" / "asset.hpp"
   if asset.exists():
      asset_source = asset.read_text(errors="ignore")
      for forbidden in ("struct asset", "struct extended_asset", "raw_pack(", "raw_unpack("):
         if forbidden in asset_source:
            errors.append(
               f"{asset.relative_to(root)}: EOSIO asset veneer must not own {forbidden.rstrip('(')}"
            )

   name = root / "guest" / "libraries" / "eosio" / "include" / "eosio" / "name.hpp"
   if name.exists():
      name_source = name.read_text(errors="ignore")
      for forbidden in ("raw_pack(", "raw_unpack("):
         if forbidden in name_source:
            errors.append(
               f"{name.relative_to(root)}: EOSIO name veneer must not own {forbidden.rstrip('(')}"
            )


def check_contract_sdk_architecture(root: Path, errors: list[str]) -> None:
   forbidden_paths = (
      root / "guest" / "cmake" / "ForgeContractGraph.cmake",
      root / "guest" / "build" / "CMakeLists.txt",
      root / "libraries" / "contract" / "graph",
      root / "tests" / "package_contract_graph_component",
   )
   for path in forbidden_paths:
      if path.exists():
         errors.append(
            f"{path.relative_to(root)}: reconstructed contract graph surface is forbidden"
         )

   reverse_graph_tokens = (
      re.compile(r"\$<LINK_ONLY:"),
      re.compile(r"::@\("),
      re.compile(r"_forge_contract_guest_dependency"),
      re.compile(r"_forge_contract_collect_registry"),
      re.compile(r"\bFORGE_CONTRACT_OWNER_IDS\b"),
      re.compile(r"contract-graph\.json"),
      re.compile(r"forge_install_contract_(?:library|package)"),
      re.compile(r"forge_register_contract_library_targets"),
   )
   architecture_sources = (
      root / "guest" / "cmake" / "ForgeContractLibraries.cmake",
      root / "guest" / "cmake" / "ForgeContractBuild.cmake",
      root / "guest" / "cmake" / "ForgeContractFunctions.cmake",
   )
   for path in architecture_sources:
      source = path.read_text(errors="ignore")
      for token in reverse_graph_tokens:
         if token.search(source):
            errors.append(
               f"{path.relative_to(root)}: Contract SDK must not reverse-parse the native CMake graph ({token.pattern})"
            )

   libraries_source = architecture_sources[0].read_text(errors="ignore")
   for property_name in ("LINK_LIBRARIES", "INTERFACE_LINK_LIBRARIES"):
      if property_name not in libraries_source:
         errors.append(
            "guest/cmake/ForgeContractLibraries.cmake: guest target immutability "
            f"must include {property_name}"
         )
      for path in architecture_sources[1:]:
         if re.search(rf"\b{property_name}\b", path.read_text(errors="ignore")):
            errors.append(
               f"{path.relative_to(root)}: Contract SDK must not inspect "
               f"{property_name}"
            )
   if re.search(
      r"get_target_property\s*\([^)]*\b(?:INTERFACE_)?LINK_LIBRARIES\b",
      libraries_source,
      re.DOTALL,
   ):
      errors.append(
         "guest/cmake/ForgeContractLibraries.cmake: dependency properties may "
         "only participate in generic immutable-state comparison"
      )

   root_cmake = (root / "CMakeLists.txt").read_text(errors="ignore")
   if re.search(r"(?m)^(?!function\()\s*forge_target_contract_guest_component\(", root_cmake):
      errors.append("CMakeLists.txt: host targets must declare guest identities beside their own definitions")
   if "forge_register_contract_guest_component" in root_cmake:
      errors.append("CMakeLists.txt: legacy central guest-component mapping is forbidden")

   attribute_plugin = root / "tools" / "attr-plugin" / "plugin.cpp"
   attribute_source = attribute_plugin.read_text(errors="ignore")
   for token in ("forge.contract.graph", "dependency_scope", "source_graph"):
      if token in attribute_source:
         errors.append(
            f"{attribute_plugin.relative_to(root)}: attribute plugin must not own contract graph policy ({token})"
         )

   guest_codec = root / "guest" / "libraries" / "codec"
   if guest_codec.exists():
      errors.append("guest/libraries/codec: guest codec forwarding family is forbidden")

   forbidden_modules = (
      "forge.core.encoding",
      "forge.crypto.base64",
      "forge.crypto.base58",
      "forge.crypto.hex",
      "forge.contract.base64",
   )
   for path in source_files(root, SCAN_ROOTS):
      source = path.read_text(errors="ignore")
      for module in forbidden_modules:
         if module in source:
            errors.append(f"{path.relative_to(root)}: removed codec module {module} is forbidden")
      for shim in ("public_key_shim", "signature_shim", "private_key_shim"):
         if shim in source:
            errors.append(f"{path.relative_to(root)}: removed asymmetric shim {shim} is forbidden")

   asymmetric_value = (
      root
      / "libraries"
      / "crypto"
      / "asymmetric"
      / "include"
      / "forge"
      / "crypto"
      / "asymmetric"
      / "values.cppm"
   )
   asymmetric_source = asymmetric_value.read_text(errors="ignore")
   if "FORGE_CONTRACT_GUEST" in asymmetric_source:
      errors.append(f"{asymmetric_value.relative_to(root)}: asymmetric values must not have host/guest definitions")

   duplicate_value_roots = (root / "libraries" / "chain" / "protocol", root / "guest" / "libraries")
   duplicate_value = re.compile(r"\b(?:class|struct)\s+(?:public_key|signature)\b|\busing\s+(?:public_key|signature)\s*=\s*std::variant")
   for value_root in duplicate_value_roots:
      for path in source_files(root, (str(value_root.relative_to(root)),)):
         if duplicate_value.search(path.read_text(errors="ignore")):
            errors.append(f"{path.relative_to(root)}: asymmetric values belong to forge.crypto.asymmetric.values")

   contract = root / "guest" / "libraries" / "contract"
   include = contract / "include" / "forge" / "contract"
   implementation_units = {
      "action",
      "authorization",
      "bitset",
      "call",
      "compatibility_asset",
      "crypto",
      "crypto_bls_ext",
      "crypto_ext",
      "deferred_transaction",
      "dispatcher",
      "instant_finality",
      "intrinsics",
      "multi_index",
      "print",
      "privileged",
      "producer_schedule",
      "rope",
      "system",
      "transaction",
   }
   for stem in sorted(implementation_units):
      if not (include / f"{stem}.cppm").exists() or not (contract / f"{stem}.cpp").exists():
         errors.append(f"guest contract implementation unit {stem} must have an exact .cppm/.cpp pair")

   header_only = {
      "binary_extension",
      "compatibility_name",
      "contract",
      "datastream",
      "fixed_bytes",
      "ignore",
      "key",
      "powers",
      "singleton",
      "string",
      "varint",
   }
   for stem in sorted(header_only):
      if (contract / f"{stem}.cpp").exists():
         errors.append(f"guest contract header-only module {stem} must not own a .cpp")

   moved_records = (
      "code_hash_result",
      "blockchain_parameters",
      "kv_parameters",
      "finalizer_authority",
      "finalizer_policy",
      "call_data_header",
   )
   for path in sorted(include.glob("*.cppm")):
      source = path.read_text(errors="ignore")
      for record in moved_records:
         if re.search(rf"\bstruct\s+{record}\b", source):
            errors.append(f"{path.relative_to(root)}: {record} belongs to forge.chain.protocol")


def check_crypto_family(root: Path, files: list[Path], errors: list[str]) -> None:
   leaf_namespaces = {
      "asymmetric",
      "bls",
      "bn256",
      "core",
      "digest",
      "math",
      "pki",
      "symmetric",
   }
   forbidden_modules = (
      "forge.crypto.types",
      "forge.crypto.secret_bytes",
      "forge.crypto.random",
      "forge.crypto.sha1",
      "forge.crypto.sha224",
      "forge.crypto.sha256",
      "forge.crypto.sha3",
      "forge.crypto.sha512",
      "forge.crypto.ripemd160",
      "forge.crypto.blake2",
      "forge.crypto.hmac",
      "forge.crypto.packhash",
      "forge.crypto.aes",
      "forge.crypto.chacha20_poly1305",
      "forge.crypto.kdf",
      "forge.crypto.asymmetric.value",
      "forge.crypto.p256",
      "forge.crypto.secp256k1",
      "forge.crypto.ed25519",
      "forge.crypto.rsa",
      "forge.crypto.webauthn",
      "forge.crypto.x25519",
      "forge.crypto.der",
      "forge.crypto.pem",
      "forge.crypto.x509",
      "forge.crypto.bigint",
      "forge.crypto.modular_arithmetic",
      "forge.crypto.base32",
      "forge.crypto.city",
   )
   root_namespace = re.compile(r"^(?:export\s+)?namespace\s+forge::crypto\s*\{")

   for path in files:
      relative = path.relative_to(root)
      for line_number, line in enumerate(path.read_text(errors="ignore").splitlines(), 1):
         if root_namespace.match(line.strip()):
            errors.append(
               f"{relative}:{line_number}: forge::crypto is a grouping namespace; "
               "public symbols must belong to a Crypto leaf"
            )
         for match in re.finditer(r"\bforge::crypto::([A-Za-z_][A-Za-z0-9_]*)", line):
            owner = match.group(1)
            if owner not in leaf_namespaces:
               errors.append(
                  f"{relative}:{line_number}: forge::crypto::{owner} bypasses the Crypto leaf namespace"
               )
         for module in forbidden_modules:
            if re.search(rf"\b{re.escape(module)}\b", line):
               errors.append(f"{relative}:{line_number}: removed Crypto module {module} is forbidden")

   cmake_files = [root / "CMakeLists.txt", root / "cmake" / "ForgeConfig.cmake.in"]
   cmake_files.extend(root.glob("libraries/**/CMakeLists.txt"))
   cmake_files.extend(root.glob("plugins/**/CMakeLists.txt"))
   cmake_files.extend(root.glob("tests/**/CMakeLists.txt"))
   cmake_files.extend(root.glob("guest/**/CMakeLists.txt"))
   cmake_files.extend(root.glob("guest/cmake/**/*.cmake"))
   cmake_files.extend(root.glob("guest/cmake/**/*.cmake.in"))
   for path in sorted(set(cmake_files)):
      source = path.read_text(errors="ignore")
      if re.search(r"\bforge_crypto\b", source):
         errors.append(f"{path.relative_to(root)}: removed Crypto aggregate target is forbidden")
      for package in re.finditer(
         r"\bfind_package\s*\(\s*Forge\b(?P<arguments>[^)]*)\)",
         source,
         flags=re.IGNORECASE | re.DOTALL,
      ):
         arguments = {
            value.casefold()
            for token in re.findall(r'"[^"]*"|\S+', package.group("arguments"))
            for value in token.strip('"').split(";")
         }
         if "crypto" in arguments:
            errors.append(f"{path.relative_to(root)}: removed Crypto package component is forbidden")

   removed_paths = (
      root / "libraries" / "crypto" / "include",
      root / "libraries" / "crypto" / "base32.cpp",
      root / "libraries" / "crypto" / "city.cpp",
      root / "libraries" / "crypto" / "city_crc.cpp",
   )
   for path in removed_paths:
      if path.exists():
         errors.append(f"{path.relative_to(root)}: removed monolithic Crypto path is forbidden")


def check_modules(root: Path, files: list[Path], errors: list[str]) -> None:
   declarations: dict[str, list[tuple[Path, int]]] = defaultdict(list)
   imports: list[tuple[str, Path, int]] = []

   for path in files:
      relative = path.relative_to(root)
      source_lines = path.read_text(errors="ignore").splitlines()
      unit_name = next((match.group(1) for line in source_lines if (match := MODULE_UNIT.match(line))), None)
      unit_primary = unit_name.split(":", 1)[0] if unit_name else None
      seen_imports: dict[str, int] = {}
      seen_includes: dict[tuple[str, tuple[tuple[int, int], ...]], int] = {}
      conditional_stack: list[list[int]] = []
      next_conditional = 0
      named_module_declared = False

      for line_number, line in enumerate(source_lines, 1):
         if CONDITIONAL_START.match(line):
            next_conditional += 1
            conditional_stack.append([next_conditional, 0])
         elif CONDITIONAL_BRANCH.match(line) and conditional_stack:
            conditional_stack[-1][1] += 1
         elif CONDITIONAL_END.match(line) and conditional_stack:
            conditional_stack.pop()

         declaration = MODULE_DECLARATION.match(line)
         if declaration:
            declarations[declaration.group(1)].append((relative, line_number))

         if MODULE_UNIT.match(line):
            named_module_declared = True

         imported = MODULE_IMPORT.match(line)
         if imported:
            name = imported.group(1)
            if name.startswith(":"):
               if unit_primary is None:
                  errors.append(f"{relative}:{line_number}: relative import has no owning module")
                  continue
               name = f"{unit_primary}{name}"
            imports.append((name, relative, line_number))
            if name in seen_imports:
               errors.append(
                  f"{relative}:{line_number}: duplicate import {name} "
                  f"(first at line {seen_imports[name]})"
               )
            else:
               seen_imports[name] = line_number

         included = INCLUDE.match(line)
         if included:
            if path.suffix == ".cppm" and named_module_declared and included.group(1).startswith("<"):
               errors.append(
                  f"{relative}:{line_number}: system header include must stay in the global module fragment"
               )
            context = tuple((block, branch) for block, branch in conditional_stack)
            key = (included.group(1), context)
            if key in seen_includes:
               errors.append(
                  f"{relative}:{line_number}: duplicate include {included.group(1)} "
                  f"in the same conditional branch (first at line {seen_includes[key]})"
               )
            else:
               seen_includes[key] = line_number

         if path.suffix == ".cppm" and BROAD_EXPORT.match(line):
            errors.append(f"{relative}:{line_number}: manual broad export block is forbidden")

   for name, owners in sorted(declarations.items()):
      if len(owners) > 1:
         locations = ", ".join(f"{path}:{line}" for path, line in owners)
         errors.append(f"module {name} has multiple declarations: {locations}")

   known_modules = set(declarations)
   for name, path, line_number in imports:
      if name not in known_modules:
         errors.append(f"{path}:{line_number}: import references unknown Forge module {name}")


def main() -> int:
   if len(sys.argv) != 2:
      print("usage: check_structure.py <repository-root>", file=sys.stderr)
      return 2

   root = Path(sys.argv[1]).resolve()
   errors: list[str] = []
   files = source_files(root, SCAN_ROOTS)

   check_layout(root, errors)
   check_aggregates(root, errors)
   check_pairing(root, errors)
   check_vm_wasm_boundaries(root, errors)
   check_plugin_impl_ownership(root, errors)
   check_chain_savanna_boundaries(root, errors)
   check_chain_api_shape(root, errors)
   check_chain_audited_api_workflow(root, errors)
   check_mdbx_module_boundary(root, errors)
   check_contract_sdk_workflow(root, errors)
   check_contract_sdk_components(root, errors)
   check_eosio_veneer(root, errors)
   check_contract_sdk_architecture(root, errors)
   check_crypto_family(root, files, errors)
   check_modules(root, files, errors)

   if errors:
      for error in sorted(set(errors)):
         print(error, file=sys.stderr)
      return 1

   print(f"Forge structure check passed ({len(files)} first-party source files scanned)")
   return 0


if __name__ == "__main__":
   raise SystemExit(main())
