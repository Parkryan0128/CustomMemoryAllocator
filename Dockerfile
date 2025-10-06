# Start from an official base image that has the GCC C++ compiler.
FROM gcc:latest

# Copy all your project files from your Mac into the container's /app directory.
COPY . /app

# Set the working directory inside the container.
WORKDIR /app

# The default command to run when the container starts.
# We will override this, but it's good practice to have one.
CMD ["bash"]