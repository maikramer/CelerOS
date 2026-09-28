cmake_minimum_required(VERSION 3.16)

# Componentes mortos do fork (BluetoothServer, Drivers, IoUtility,
# SafeContainers, Supabase, UI, UserManaging, Time — ~19k linhas fora do
# build desde sempre) foram REMOVIDOS na F3: o historico segue no git.
# Placa alvo: "smartdisplay" (Guition S3 4848S040) ou "cyd" (ESP32-2432S028R).
# Passar no configure: idf.py -DCELEROS_BOARD=cyd ...
set(CELEROS_BOARD "smartdisplay" CACHE STRING "CelerOS target board: smartdisplay|cyd")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(CelerOS VERSION 1.2.0)
