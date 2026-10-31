{
  description = "todo.md: a desktop front-end for markdown tasks";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      forAll = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in
    {
      packages = forAll (pkgs: rec {
        todomd = pkgs.callPackage ./package.nix { };
        default = todomd;
      });

      overlays.default = final: prev: { todomd = final.callPackage ./package.nix { }; };

      devShells = forAll (pkgs: {
        default = pkgs.mkShell {
          inputsFrom = [ self.packages.${pkgs.stdenv.hostPlatform.system}.todomd ];
        };
      });
    };
}
