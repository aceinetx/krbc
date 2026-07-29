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

          buildPhase = ''
            cc krbc.c -o krbc -g -std=c99 -Wall -Wextra -Wpedantic -fsanitize=address
          '';

          buildInputs = with pkgs; [
            gcc
          ];

          installPhase = ''
            mkdir -p $out/bin
            cp krbc $out/bin/
          '';
        };

        devShells.default = pkgs.mkShell {
          buildInputs = with pkgs; [
            gcc
          ];
        };
      }
    );
}
