cmake_minimum_required(VERSION 3.5)
project(gtthreads)

set(CMAKE_CXX_STANDARD 11)

# gtthreads library
add_library(gtthreads
        src/gt_bitops.h
        src/gt_include.h
        src/gt_kthread.c
        src/gt_kthread.h
        src/gt_matrix.c
        src/gt_pq.c
        src/gt_pq.h
        src/gt_signal.c
        src/gt_signal.h
        src/gt_spinlock.c
        src/gt_spinlock.h
        src/gt_tailq.h
        src/gt_uthread.c
        src/gt_uthread.h)

target_compile_definitions(gtthreads PUBLIC -DDEBUG=1)

# matrix
add_executable(matrix src/gt_matrix.c)

add_dependencies(matrix gtthreads)

target_link_libraries(matrix gtthreads)

# gtpq (testing)
add_executable(gtpq src/gt_pq.c)

target_compile_definitions(gtpq PRIVATE -DPQ_DEBUG)

add_dependencies(gtpq gtthreads)

target_link_libraries(gtpq gtthreads)

