####################################################################
# Usage:                                                           #
# 	docker build -t name:tag .                                     #
#   docker run -d -p <host_port>:8888 name:tag                     #
#   nc localhost <host_port>                                       #
####################################################################

FROM ubuntu:18.04
MAINTAINER kruztw

RUN apt-get update
RUN apt-get install -y xinetd
RUN useradd -m kruztw

COPY bin /home/kruztw
COPY simple.xinetd /etc/xinetd.d/simple

EXPOSE 8888
CMD ["/home/kruztw/start.sh"]