# VS Code + ESP-IDF v5.5.6 setup (project path with a space)

Project: `/Users/baratamgurumanohargupta/Desktop/My Projects/ProtoScope`

## Why a build mirror
ESP-IDF does not support spaces in the project path. Instead of renaming your
folder, `tools/idf_phase1.sh` copies the source (not build output) to
`~/esp/protoscope-build` before every command and builds/flashes from there.
Edit only in the Desktop folder; the mirror is disposable and overwritten each
time. Build output, `sdkconfig` and `dependencies.lock` live in the mirror.
(Renaming `My Projects` to `My_Projects` would make the mirror unnecessary.)

## Auto-activate ESP-IDF in every VS Code terminal
`.vscode/settings.json` sets the default macOS terminal profile
"ProtoScope IDF 5.5.6", which runs `source ./tools/activate_idf.sh`. Open the
`ProtoScope` folder itself in VS Code (File > Open Folder) and open a new
terminal (Ctrl+`); it prints `ProtoScope: ESP-IDF v5.5.6 active, target esp32s3.`
`tools/activate_idf.sh` pins `~/esp/esp-idf-v5.5.6` and its 5.5 Python
environment, so a 5.4/6.x install can never be picked up by mistake.

## Commands (run from the project folder)
```
./tools/test_host.sh                  # host tests, no ESP-IDF needed
./tools/verify_phase3.sh              # host tests + firmware build
./tools/idf_phase1.sh build
./tools/idf_phase1.sh -p /dev/cu.usbserial-XXXX flash monitor    # Ctrl+] exits
```
Do not paste `# comments` into zsh. VS Code tasks (Terminal > Run Task) cover
the same commands.
