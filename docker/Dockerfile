FROM ubuntu:latest

# Update Ubuntu Software repository
RUN apt update && \
apt install -y wget make cmake clang libc++-dev libc++abi-dev libspdlog-dev libssl-dev

RUN cd /root/ && \
wget http://boostorg.jfrog.io/artifactory/main/release/1.81.0/source/boost_1_81_0.tar.gz && \
tar xzf boost_1_81_0.tar.gz boost_1_81_0/boost && \
mv boost_1_81_0/boost /usr/local/include/

CMD ["/bin/bash"]
