# For non-flake users: nix-build, or nix-env -if .
{
  pkgs ? import <nixpkgs> { },
}:
pkgs.callPackage ./package.nix { }
