# Start from an official base image that has the GCC C++ compiler.
FROM gcc:latest

# Install Python and the necessary plotting libraries
RUN apt-get update && apt-get install -y python3 python3-pip python3-venv
RUN python3 -m venv /opt/venv
RUN /opt/venv/bin/pip install pandas matplotlib seaborn

# Copy all your project files from your Mac into the container's /app directory.
COPY . /app

# Set the working directory inside the container.
WORKDIR /app

# The default command to run when the container starts.
ENV PATH="/opt/venv/bin:$PATH"

CMD ["bash"]
