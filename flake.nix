{
  description = "gf2";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in
      {
        packages.default = pkgs.stdenv.mkDerivation {
          name = "krbc";
          version = "1.0.0";
          src = self;

          buildInputs = with pkgs; [
            stdenv.cc.cc
          ];

          buildPhase = ''
            cc krbc.c -o krbc       -std=c99 -O3                           -Wall -Wextra -Wpedantic
            cc krbc.c -o krbc_debug -std=c99 -O0 -g -fsanitize=address -lm -Wall -Wextra -Wpedantic
          '';

          installPhase = ''
            mkdir -p $out/bin
            mkdir -p $out/include

            cp krbc       $out/bin/
            cp krbc_debug $out/bin/

            cp krbc.h $out/include

            mkdir -p $out/share/man/man1
            install -m 444 ./krbc.1 $out/share/man/man1/krbc.1
          '';
        };

        devShells.default = pkgs.mkShell {
          buildInputs = with pkgs; [
            stdenv.cc.cc
          ];
        };
      }
    );
}
