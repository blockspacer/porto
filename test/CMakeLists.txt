project(tests)

include_directories(${util_SOURCE_DIR})
include_directories(${libporto_SOURCE_DIR})
include_directories(${porto_SOURCE_DIR})
include_directories(${porto_BINARY_DIR})

add_executable(portotest portotest.cpp test.cpp selftest.cpp stresstest.cpp)

target_link_libraries(portotest version porto util config pthread rt fmt ${PB} ${LIBNL} ${LIBNL_ROUTE})

add_executable(mem_touch mem_touch.c)

add_executable(test-api test-api.cpp)
target_link_libraries(test-api porto pthread ${PB})

macro(ADD_PYTHON_TEST NAME)
         add_test(NAME ${NAME}
                  COMMAND sudo python -uB ${CMAKE_SOURCE_DIR}/test/test-${NAME}.py
                  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endmacro()

macro(ADD_PYTHON3_TEST NAME)
         add_test(NAME python3-${NAME}
                  COMMAND sudo python3 -uB ${CMAKE_SOURCE_DIR}/test/test-${NAME}.py
                  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endmacro()

# head

add_test(NAME portod_start
         COMMAND sudo ${CMAKE_SOURCE_DIR}/test/start
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# interface tests

ADD_PYTHON_TEST(api)

ADD_PYTHON3_TEST(api)

add_test(NAME cpp-api
         COMMAND sudo ${CMAKE_BINARY_DIR}/test-api
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

ADD_PYTHON_TEST(wait)
ADD_PYTHON3_TEST(wait)

if(EXISTS /usr/bin/go AND EXISTS /usr/share/gocode/src/github.com/golang/protobuf)
add_test(NAME go_api
         COMMAND sudo go test -v api/go/porto
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(go_api
                     PROPERTIES
                     ENVIRONMENT GOPATH=${CMAKE_SOURCE_DIR}:/usr/share/gocode)
endif()

ADD_PYTHON_TEST(ct-state)
ADD_PYTHON_TEST(properties)
ADD_PYTHON_TEST(knobs)
ADD_PYTHON_TEST(labels)
ADD_PYTHON_TEST(ulimit)
ADD_PYTHON_TEST(place)
ADD_PYTHON_TEST(mount)
ADD_PYTHON_TEST(aufs)
ADD_PYTHON_TEST(symlink)
ADD_PYTHON_TEST(devices)
ADD_PYTHON_TEST(systemd)
ADD_PYTHON_TEST(os)
ADD_PYTHON_TEST(jobs)

ADD_PYTHON_TEST(portoctl-exec)
ADD_PYTHON_TEST(portoctl-wait)
ADD_PYTHON_TEST(self-container)
ADD_PYTHON_TEST(portoctl-attach)

# limits test

ADD_PYTHON_TEST(io-stat)
ADD_PYTHON_TEST(io-limit)

ADD_PYTHON_TEST(net-sched)

ADD_PYTHON_TEST(mem-overcommit)
ADD_PYTHON_TEST(mem_limit_total)
ADD_PYTHON_TEST(mem-recharge)
ADD_PYTHON_TEST(dirty-limit)
ADD_PYTHON_TEST(cpu_limit)

# functional tests

ADD_PYTHON_TEST(uid_handling)
ADD_PYTHON_TEST(unpriv-cred)
ADD_PYTHON_TEST(isolation)
ADD_PYTHON_TEST(security)
ADD_PYTHON_TEST(net)
ADD_PYTHON_TEST(tc-classes)
ADD_PYTHON_TEST(tc-rebuild)
ADD_PYTHON_TEST(locate-process)
ADD_PYTHON_TEST(prev_release_upgrade)
ADD_PYTHON_TEST(oom)
ADD_PYTHON_TEST(hugetlb)
ADD_PYTHON_TEST(coredump)

ADD_PYTHON_TEST(volume-restore)

# legacy tests

add_test(NAME portotest
         COMMAND sudo ${CMAKE_BINARY_DIR}/portotest --except recovery wait_recovery leaks
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

ADD_PYTHON_TEST(legacy-root-loop)

# slow tests

ADD_PYTHON_TEST(mem_limit)
ADD_PYTHON_TEST(volume_backends)
ADD_PYTHON_TEST(volume_places)
ADD_PYTHON_TEST(volume_links)
ADD_PYTHON_TEST(portod_cli)
ADD_PYTHON_TEST(recovery)

add_test(NAME leaks
         COMMAND sudo ${CMAKE_BINARY_DIR}/portotest leaks
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

ADD_PYTHON_TEST(performance)

add_test(NAME fuzzer_soft
         COMMAND sudo python -uB ${CMAKE_SOURCE_DIR}/test/fuzzer.py --no-kill
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

add_test(NAME fuzzer
         COMMAND sudo python -uB ${CMAKE_SOURCE_DIR}/test/fuzzer.py
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# tail

ADD_PYTHON_TEST(stats)

add_test(NAME portod_stop
         COMMAND sudo ${CMAKE_BINARY_DIR}/portod stop
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# end
