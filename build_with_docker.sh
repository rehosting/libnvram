#!/bin/bash


docker run --rm -v $PWD:/app -w /app  ghcr.io/rehosting/embedded-toolchains:latest /app/package.sh
