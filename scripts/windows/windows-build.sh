cmake -S . -B build-pacloader -G "MinGW Makefiles" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-pacloader --parallel "$(nproc)"

rm -rf win-release
mkdir win-release
cp build-pacloader/linuxloader.exe win-release/linuxloader.exe
cp libs/win32/SDL3.dll win-release/SDL3.dll
mkdir win-release/ll-deps
while IFS= read -r dependency; do
    case "$dependency" in ''|'#'*) continue ;; esac
    mkdir -p "win-release/$(dirname "$dependency")"
    cp "libs/win32/$dependency" "win-release/$dependency"
done < packaging/dependencies/wmmt3.txt
cp "$(dirname "$(command -v gcc)")/libgcc_s_dw2-1.dll" win-release/ll-deps/

cd win-release
zip -r ../linuxloader-win32.zip .
cd ..
