# Build: fxsdk build-cg
cmake_minimum_required(VERSION 3.15)
project(NoteView)

include(GenerateG3A)
find_package(Gint 2.9 REQUIRED)

add_executable(photos src/main.c src/view.c src/tjpgd.c)
target_compile_options(photos PRIVATE -Wall -Wextra -O2 -flto)
target_link_libraries(photos Gint::Gint)

generate_g3a(TARGET photos OUTPUT "NoteView.g3a"
  NAME "NoteView" ICONS assets-cg/icon-uns.png assets-cg/icon-sel.png)
