{
  description = "oshot; a program to screenshot and get text from images";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs?ref=nixos-unstable";
  };

  outputs = { self, nixpkgs, ... }:
    let 
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };
      version = "0.5.0-rc1";
    in
    {
      packages.${system} = {
        oshot = pkgs.stdenv.mkDerivation {
          pname = "oshot";
          inherit version;
          src = ./.;

          nativeBuildInputs = with pkgs; [
            cmake
            ninja
            pkg-config
            wrapGAppsHook3
            wayland-scanner
          ];

          buildInputs = with pkgs; [
            glfw3
            tesseract
            leptonica
            zbar
            libGL
            libpng

            libx11
            libxcb
            libxrandr
            glib
            gtk3
            libappindicator-gtk3
            
            systemd
            libarchive
            curl
          ];

          preBuild = ''
            export HASH="nix-build"
            export BRANCH="nix"
            export MESSAGE="built by nix"
            export DATE="1970-01-01"
            export DIRTY="clean"
            export TAG="${version}"
            export COMMITS="0"

            ./scripts/generateVersion.sh
          '';

          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
          ];

          installPhase = ''
            cmake --install . --prefix "$out"
          '';
        };

        default = self.packages.${system}.oshot;
      };
    };
}
