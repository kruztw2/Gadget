# IPIP tunnel 

<b>reference: https://www.cnblogs.com/bakari/p/10564347.html</b>

![](https://img2018.cnblogs.com/blog/431521/201903/431521-20190320132302980-826956388.png)


## Usage

make sure you have root privilege

### setup
./simple.sh start

### remove
./simple.sh clean

### test
open wireshark and listen on v1_r

ip netns exec ns1 ping 10.10.200.10

for more detail please check the reference.
