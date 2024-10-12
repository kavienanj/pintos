# Pintos Docker Setup

This Dockerfile is designed to set up Pintos, an educational operating system framework for the x86 architecture, in any operating system using Docker. It provides a straightforward way to get Pintos running without the need to manually configure the development environment.

## Prerequisites

Before you begin, ensure you have Docker installed on your system. If you do not have Docker installed, follow the instructions on the [official Docker website](https://docs.docker.com/get-docker/) to install it.

## Getting Started

To use this Docker setup for Pintos, follow these steps:

1. **Clone the Repository**

   First, clone this repository to your local machine using:

   ```bash
   git clone https://github.com/kavienanj/pintos.git
   ```

2. **Build the Docker Image**

   Navigate to the directory containing the Dockerfile and build the Docker image using:

   ```bash
   docker build -t pintos .
   ```

   This command builds a Docker image named `pintos` based on the instructions in the Dockerfile. You can replace `pintos` with any other name you prefer.

3. **Run the Docker Container**

   After the image has been successfully built, you can start a Docker container with the Pintos development environment using:

   ```bash
   docker run --rm -it pintos
   ```

   This command starts a new container and opens an interactive terminal session inside it. You are now in an environment where Pintos is set up and ready to use.

4. **Verify Pintos Installation**

   After entering the Docker container, you can verify that Pintos is correctly installed and working by running:

   ```bash
   make
   cd build
   pintos --
   ```

   This command runs Pintos with QEMU. If you prefer to use Bochs instead, you can run:

   ```bash
   pintos --bochs
   ```

   These commands should execute without errors, indicating that Pintos is ready for development inside the Docker container.

## Working with Pintos

To ensure that your changes to Pintos projects are saved on your host machine, you should mount the directory from your project into the corresponding directory in the Docker container. This allows you to work directly on your files using the Docker container's tools without losing changes when the container stops.

Run the following command to start the Docker container with the source directory mounted:

```bash
docker run -it --rm --mount type=bind,source=/Users/ovindu/Desktop/Repos/pintos/pintos/src,target=/pintos/src pintos
```

Replace `/path/to/your/pintos/src` with the actual path to the `src` directory in your Pintos project on your host machine. Now, any changes you make inside the `/pintos/src` directory will be reflected in the `src` directory on your host machine, allowing you to seamlessly work across both environments.

Then run the following commands to build and test Pintos:

```bash
make
cd build
pintos --
```

## Customization

If you need to customize the Docker environment, you can modify the Dockerfile and rebuild the image using the steps provided above. This allows you to add additional packages or change the configuration to suit your needs.

## Contributing

Contributions to improve the Docker setup for Pintos are welcome. Please feel free to submit pull requests or open issues if you have suggestions or encounter any problems.
