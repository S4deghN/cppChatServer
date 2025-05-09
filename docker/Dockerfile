FROM ubuntu:24.04

# Update Ubuntu Software repository
RUN apt-get update && \
apt-get install -y wget tar make cmake clang libc++-dev libc++abi-dev libssl-dev && \
rm -rf /var/lib/apt/lists/*

ARG boost_ver=1.88.0
ENV boost_file=boost_${boost_ver//./_}
RUN cd /usr/local/include/ && \
wget https://archives.boost.io/release/$boost_ver/source/$boost_file.tar.gz && \
tar xzf $boost_file.tar.gz $boost_file/boost --strip-components 1 && \
rm $boost_file.tar.gz

CMD ["/bin/bash"]
