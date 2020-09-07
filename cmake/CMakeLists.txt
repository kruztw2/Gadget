#project name
PROJECT(test_math)

#head file path
INCLUDE_DIRECTORIES( include )

#source directory
AUX_SOURCE_DIRECTORY(src SRC_DIR)

#set environment variable
SET(SRC_PATH ${SRC_DIR})

#set extern libraries
SET(LIBRARIES libm.so)

#add executable file
ADD_EXECUTABLE(./bin/elf ${SRC_PATH})

#add link library
TARGET_LINK_LIBRARIES(./bin/elf ${LIBRARIES})
