#!/bin/bash
# Builds and runs Docker container
docker build . -t project1
docker run -v $(pwd)/gtthreads:/gtthreads -it project1:latest /bin/bash
