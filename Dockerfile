# Use an official Ubuntu as the base image
FROM ubuntu:18.04
LABEL maintainer="Jegatheesan Kavienan <kavienanj@gmail.com>"

# Set non-interactive mode for apt-get to avoid prompts
ENV DEBIAN_FRONTEND=noninteractive

# Update package lists, upgrade installed packages, and install necessary packages
RUN apt-get update && \
    apt-get upgrade -y && \
    apt-get install -y --no-install-recommends \
    build-essential \
    automake \
    git \
    libncurses5-dev \
    texinfo \
    qemu \
    libvirt-bin \
    perl \
    cgdb \
    ctags \
    cscope \
    vim \
    ca-certificates \
    wget && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# Reset DEBIAN_FRONTEND
ENV DEBIAN_FRONTEND=

# Copy the Pintos src directory to the image
COPY pintos/src /pintos/src

# Build the toolchain with a custom prefix
RUN /pintos/src/misc/toolchain-build.sh --prefix /pintos/toolchain/x86_64 /pintos/toolchain && \
    echo 'export PATH=/pintos/toolchain/x86_64/bin:$PATH' >> ~/.bashrc

# Set the environment variables
ENV PATH="/pintos/toolchain/x86_64/bin:${PATH}"

# Install Pintos utilities
RUN cd /pintos/src/utils && \
    make && \
    cp backtrace pintos Pintos.pm pintos-gdb pintos-set-cmdline pintos-mkdisk setitimer-helper squish-pty squish-unix /pintos/toolchain/x86_64/bin && \
    mkdir -p /pintos/toolchain/x86_64/misc && \
    cp ../misc/gdb-macros /pintos/toolchain/x86_64/misc

# Build Threads
RUN cd /pintos/src/threads && \
    make

# Set the working directory
WORKDIR /pintos/src/threads

# Set the entrypoint
ENTRYPOINT ["/bin/bash"]
