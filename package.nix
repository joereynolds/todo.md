{
  lib,
  stdenv,
  cmake,
  ninja,
  qt6,
}:

stdenv.mkDerivation {
  pname = "todomd";
  # Single source of truth: the project() version in CMakeLists.txt.
  version = builtins.head (
    builtins.match ".*project\\(todomd VERSION ([0-9.]+) .*" (builtins.readFile ./CMakeLists.txt)
  );

  src = lib.fileset.toSource {
    root = ./.;
    fileset = lib.fileset.unions [
      ./CMakeLists.txt
      ./src
      ./tests
      ./packaging
    ];
  };

  nativeBuildInputs = [
    cmake
    ninja
    qt6.wrapQtAppsHook
  ];

  # Widgets, Concurrent and Test all live in qtbase.
  buildInputs = [ qt6.qtbase ];

  doCheck = true;
  preCheck = ''
    export QT_QPA_PLATFORM=offscreen
    export HOME=$TMPDIR
  '';

  meta = {
    description = "Desktop front-end for markdown tasks";
    homepage = "https://github.com/joereynolds/todo.md";
    mainProgram = "todomd";
    platforms = lib.platforms.linux;
  };
}
