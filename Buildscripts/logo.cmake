get_filename_component(VERSION_TEXT_FILE version.txt ABSOLUTE)

file(READ ${VERSION_TEXT_FILE} AEGIS_VERSION)
string(STRIP "${AEGIS_VERSION}" AEGIS_VERSION)

if (DEFINED ENV{ESP_IDF_VERSION})
    set(AEGIS_TARGET "   @ ESP-IDF")
else ()
    set(AEGIS_TARGET "  @ Simulator")
endif ()

string(ASCII 27 Esc)
set(ColorReset "${Esc}[m")
set(Cyan "${Esc}[36m")
set(Grey "${Esc}[37m")
set(LightPurple "${Esc}[1;35m")
set(White "${Esc}[1;37m")

# Some terminals (e.g. GitHub Actions) reset colour for every in a multiline message(),
# so we add the colour to each line instead of assuming it would automatically be re-used.
message("\n\n\
                     ${LightPurple}___\n\
                    ${LightPurple}/   \\\n\
                   ${LightPurple}/  ^  \\\n\
                  ${LightPurple}/  /_\\  \\\n\
                 ${LightPurple}/  _____  \\\n\
     ${Cyan}    /__/     \\__\\                ${White}Aegis ${AEGIS_VERSION}\n\
    ${Cyan}       ||  ___  ||                 ${Grey}${AEGIS_TARGET}\n\
              ${Cyan}|| |   | ||\n\
              ${Cyan}|| |___| ||\n\
              ${Cyan}||  ___  ||\n\
              ${Cyan}|| |   | ||\n\
              ${Cyan}||_|   |_||\n\n${ColorReset}")
