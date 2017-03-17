############################################################
#
# Update with idg4400 support dependencies.
#
############################################################
FROM opennetworklinux/builder8:1.5
MAINTAINER Noam Camus <noamca@mellanox.com>

RUN echo 'Acquire::Check-Valid-Until "false";' | tee /etc/apt/apt.conf.d/99mellanox
RUN echo "deb http://snapshot.debian.org/archive/debian/20170301T033045Z/ jessie main" | tee /etc/apt/sources.list
RUN echo "deb http://snapshot.debian.org/archive/debian/20170301T033045Z/ jessie-updates main" | tee -a /etc/apt/sources.list
RUN echo "deb http://snapshot.debian.org/archive/debian-security/20170301T002306Z/ jessie/updates main" | tee -a /etc/apt/sources.list
RUN apt-get update

#
# Docker shell and other container tools.
#
COPY docker_shell /bin/docker_shell
COPY container-id /bin/container-id
