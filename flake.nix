{
  description = "fixure: Ultra-fast, zero-dependency, multiplatform C++23 test harness and mock container for FIX protocol";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, utils }:
    utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
      in
      {
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "fixure";
          version = "0.1.0";
          src = ./.;

          nativeBuildInputs = with pkgs; [
            cmake
            ninja
          ];

          buildInputs = with pkgs; [
          ];

          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
          ];
        };

        devShells.default = pkgs.mkShell {
          packages = with pkgs; [
            cmake
            ninja
            clang-tools
            lldb
          ];

          shellHook = ''
            echo "=== Welcome to the fixure development shell ==="
            echo "Compiler: $(clang --version | head -n 1)"
            echo "CMake: $(cmake --version | head -n 1)"
          '';
        };
      });
}
