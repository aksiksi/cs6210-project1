#!/bin/bash
# Builds and runs Docker container
docker build . -t project1

# -v: map a dir to /gtthreads within container
docker run --privileged -v $(pwd)/gtthreads:/gtthreads -it project1:latest /bin/bash
