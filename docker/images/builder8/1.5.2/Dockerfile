############################################################
#
# Update with idg4400 support dependencies.
#
############################################################
FROM mellanox/idg4400:1.1
MAINTAINER Noam Camus <noamca@mellanox.com>

RUN curl http://apt.mellanox.com/debian/support@mellanox.com.gpg.key | sudo apt-key add -
RUN echo "deb http://apt.mellanox.com/debian/ unstable/amd64/" | tee /etc/apt/sources.list.d/mellanox.list
RUN echo "deb http://apt.mellanox.com/debian/ unstable/all/" | tee -a /etc/apt/sources.list.d/mellanox.list
RUN echo "deb-src http://apt.mellanox.com/debian/ unstable/source/" | tee -a /etc/apt/sources.list.d/mellanox.list
RUN apt-get update

#
# Docker shell and other container tools.
#
COPY docker_shell /bin/docker_shell
COPY container-id /bin/container-id
