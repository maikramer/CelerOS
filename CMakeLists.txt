cmake_minimum_required(VERSION 3.16)

# Placa alvo: "smartdisplay" (Guition S3 4848S040) ou "cyd" (ESP32-2432S028R).
# Passar no configure: idf.py -DCELEROS_BOARD=cyd ...
set(CELEROS_BOARD "smartdisplay" CACHE STRING "CelerOS target board: smartdisplay|cyd")


include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(CelerOS VERSION 1.3.0)
