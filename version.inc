# Included by CMakeLists, edited by the build script
# (the version numbers are generated by the build script from the git current label)

set(SLIC3R_APP_NAME "QIDIStudio")
set(SLIC3R_APP_KEY "QIDIStudio")

if(NOT DEFINED QDT_RELEASE_TO_PUBLIC)
set(QDT_RELEASE_TO_PUBLIC "0")
endif()
if(NOT DEFINED QDT_INTERNAL_TESTING)
set(QDT_INTERNAL_TESTING "1")
endif()

# The build_version should start from 50 in master branch

set(SLIC3R_VERSION "02.00.03.56")

string(REPLACE "." "," SLIC3R_COMMA_SEPARATED_VERSION ${SLIC3R_VERSION})
set(SLIC3R_COMMA_SEPARATED_VERSION "${SLIC3R_COMMA_SEPARATED_VERSION}")
