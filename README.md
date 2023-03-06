Specification of the jess(Jsonify analog Environment Synchronous Services) protocol:
1.  The protocol uses plain text mode
2.  The text base unit of the protocol is line, with CRLF as the divider for each line.
3.  Blank lines are allowed to exist in any case but are always ignored
4.  The generic protocol format like the following, use two consecutive CRLF to mark end of request option,
    all options that cannot be parsed by the server will be return the original text to the requester.
    [command CRLF] [option CRLF...CRLF] [data...]
5.  text status can be one of the following:
    ok
    fatal
*
6.  config-pair request command list:
     6.1  basic protocol format of read request:                         [read CRLF] [option CRLF...CRLF] [key CRLF...]
     6.2  basic protocol format for write requests:                      [write CRLF] [option CRLF...CRLF] [key=value CRLF...]
     6.3  scope read basic protocol format:                              [scope CRLF] [option CRLF...CRLF] [key CRLF]
     6.4  direct request file data:                                      [file CRLF] [option CRLF...CRLF] [key CRLF]
     6.5  query key list in next range:("/" for root)                    [contain CRLF] [option CRLF...CRLF] [key CRLF]
     6.6  subscribe concerned pair change:                               [subscribe CRLF] [option CRLF...CRLF] [key CRLF...]
     6.7  unsubscribe subscribed pair:                                   [unsubscribe CRLF] [option CRLF...CRLF] [key CRLF...]
7.  binary request command list:
     5.1  option offset=@int RECOMMAND be set in request
     5.2  option size=@int MUST be set in request
     5.1  binary read:                                                   [rbin CRLF] [option CRLF...CRLF]
     5.2  binary save:                                                   [wbin CRLF] [option CRLF...CRLF] [binary-data]
8.  text model request command list:
9.  management command list:
     9.1 create directory level node:                                    [mkdir CRLF] [option CRLF...CRLF] [dir CRLF]
     9.2 remove directory level node (include all it's sub scopes):      [rmdir CRLF] [option CRLF...CRLF] [dir CRLF]
     9.3 keepalive                                                       [keepalive CRLF]
     9.4 login                                                           [login CRLF] [option CRLF...CRLF] ...
     9.5 create a checkpoint to save current config settings             [checkpoint CRLF] [option CRLF...CRLF]
     9.6 recover config settings by speify checkpoing file               [recover CRLF] [option CRLF...CRLF] [file CRLF]
     9.7 force sync data into disk file                                  [sync CRLF] [option CRLF...CRLF]
*
10. config-pair response list:
    10.1  Basic protocol format of read response:                        [read CRLF] [option CRLF...CRLF] [[status CRLF key=value CRLF]...]
    10.2  Basic protocol format for write response:                      [write CRLF] [option CRLF...CRLF] [status CRLF...]
    10.3  Basic protocol format for scope response:                      [scope CRLF] [option CRLF...CRLF] [status CRLF [key=value CRLF...]]
    10.4  Basic protocol format for file response:                       [file CRLF] [option CRLF...CRLF] [status CRLF] [binary-data]
    10.5  Basic protocol format for contain keys response:               [contain CRLF] [option CRLF...CRLF] [status CRLF [key CRLF...]]
    10.6  basic protocol format for subscribe response:                  [subscribe CRLF] [option CRLF...CRLF] [status CRLF...]
    10.7  basic protocol format for unsubscribe response:                [unsubscribe CRLF] [option CRLF...CRLF] [status CRLF...]
    10.8  basic protocol format for publish:                             [publish CRLF] [option CRLF...CRLF] [key=value CRLF...]
11.  binary response list
    11.1  basic protocol format for binary read:                         [rbin CRLF] [option CRLF...CRLF] [status CRLF] [binary-data]
    11.2  basic protocol format for binary write:                        [wbin CRLF] [option CRLF...CRLF] [status CRLF]
13. text model response list:
14. management response list:
    14.1 basic protocol format for create directory:                     [mkdir CRLF] [option CRLF...CRLF] [status CRLF]
    14.2 basic protocol format for remove directory:                     [rmdir CRLF] [option CRLF...CRLF] [status CRLF]
    14.3 basic protocol format for keepalive:                            [keepalive CRLF]
    14.4 basic protocol format for checkpoint:                           [checkpoint CRLF] [option CRLF...CRLF] [status CRLF]
    14.5 basic protocol format for recover:                              [recover CRLF] [option CRLF...CRLF] [status CRLF]
    14.6 basic protocol format for sync:                                 [sync CRLF] [option CRLF...CRLF] [status CRLF]
*
15. test case for read command
    15.1 normal and multiple key read request:
    $ echo -ne 'Nspd\x4F\x0\x0\x0read\r\ndriver.dio.device[0].merge\r\nmotion.navigation.upl_mapping_angle_tolerance' | nc -4 127.0.0.1 4407
    Nspd_
    read
    ok
    driver.dio.device[0].merge=0
    ok
    motion.navigation.upl_mapping_angle_tolerance=1

    15.2 read request associated with option data:
    $ echo -ne 'Nspd\x47\x0\x0\x0read\r\nid=1\r\ntoken=20\r\n\r\ndriver.dio.device[1].ais.block[0].start_address' | nc -4 127.0.0.1 4407
    NspdV
    read
    id=1
    token=20

    ok
    driver.dio.device[1].ais.block[0].start_address=0x2101

    15.3 non logic node read request:
    $ echo -ne 'Nspd\x10\x0\x0\x0read\r\ndriver.dio' | nc -4 127.0.0.1 4407
    Nspd1
    read
    ok
    driver.dio=/etc/agv/driver/dio.json

    15.4 using wildcard to query array size
    $ echo -ne 'Nspd\x1A\x0\x0\x0read\r\ndriver.dio.device[?]' | nc -4 127.0.0.1 4407
    Nspd$
    read
    ok
    driver.dio.device[?]=2

16. test case for write command
    16.1 exec the command
    $ echo -ne 'Nspd\x3D\x0\x0\x0write\r\ndriver.dio.device[1].ais.block[$].start_address=0x3000' | nc -4 127.0.0.1 4407
    Nspd
    write
    ok

    16.2 using keyword 'deleted' to delete a field/scope/file/array
    $ echo -ne 'Nspd\x24\x0\x0\x0write\r\ndriver.dio.device[0].latency=deleted' | nc -4 127.0.0.1 4407
    Nspd
    write
    ok

    16.3 using wildcard to add a item upon specify array
    $ echo -ne 'Nspd\x3D\x0\x0\x0write\r\ndriver.dio.device[1].ais.block[+].start_address=0x4000' | nc -4 127.0.0.1 4407
    Nspd
    write
    ok

17. test case for scope command
    17.1 normal scope read test:
    $ echo -ne 'Nspd\x11\x0\x0\x0scope\r\ndriver.dio' | nc -4 127.0.0.1 4407
    Nspd
    scope
    ok
    dio.device[0].id=199
    dio.device[0].di_channel_num=0
    dio.device[0].aos.block[0].internel_type=3
    dio.device[0].aos.block[0].effective=8
    dio.device[0].aos.block[0].start_address=0x000
    dio.device[0].ais.block[0].internel_type=16
    dio.device[0].ais.block[0].effective=8
    dio.device[0].ais.block[0].start_address=0x2170
    dio.device[0].can=101
    dio.device[0].do_channel_num=0
    dio.device[0].name=red
    dio.device[0].latency=0
    dio.device[0].merge=0
    dio.device[0].node=35
    dio.device[0].port=0
    dio.device[1].id=198
    dio.device[1].di_channel_num=1
    dio.device[1].aos.block[0].internel_type=3
    dio.device[1].aos.block[0].effective=8
    dio.device[1].aos.block[0].start_address=0x00
    dio.device[1].ais.block[0].internel_type=32
    dio.device[1].ais.block[0].effective=0
    dio.device[1].ais.block[0].start_address=0x3000
    dio.device[1].ais.block[1].start_address=0x4000
    dio.device[1].can=101
    dio.device[1].do_channel_num=1
    dio.device[1].name=dio
    dio.device[1].latency=0
    dio.device[1].merge=1
    dio.device[1].node=16
    dio.device[1].port=0

    17.2 fatal on trying to read un exist scope:
    $ echo -ne 'Nspd\x14\x0\x0\x0scope\r\ndriver.dio197' | nc -4 127.0.0.1 4407
    Nspd
    scope
    fatal

18. test case for contain command
    18.1 normal test read contains in file:
    $ echo -ne 'Nspd\xf\x0\x0\x00contain\r\ndriver' | nc -4 127.0.0.1 4407
    Nspd
    contain
    ok
    elmo
    curtis
    canbus
    angle_encoder
    copley
    driveunit
    dio
    dwheel
    swheel
    omron_plc
    moos
    sddex
    swheel_angle_link
    voice

    18.2 success read contains of root directory
    $ echo -ne 'Nspd\xa\x0\x0\x00contain\r\n/' | nc -4 127.0.0.1 4407
    NspdP
    contain
    ok
    foundation
    driver
    custom
    embedded
    map
    localization
    motion

    18.3 success read contains from multiple parent section:
    $ echo -ne 'Nspd\x1f\x0\x0\x00contain\r\ndriver\r\nmotion.vehicle' | nc -4 127.0.0.1 4407
    Nspd¿
    contain
    ok
    dio199
    copley102
    angle_encoder201
    angle_encoder200
    canbus100
    curtis161
    copley103
    dio198
    moos102
    dwheel301
    driveunit31
    driveunit30
    dwheel300
    elmo131
    elmo130
    elmo132
    swheel400
    omron_plc
    moos103
    sddex
    swheel_angle_link
    swheel401
    voice
    ok
    max_dec
    id
    creep_speed
    chassis_type
    creep_w
    max_acc
    max_acc_w
    name
    max_speed
    max_dec_w
    max_w
    vehicle_name
    vehicle_id
    steer_angle_error_tolerance
    vehicle_type

19. test case for file command
    19.1 fatal on giving section are not a file scope:
    $ echo -ne 'Nspd\x19\x0\x0\x00file\r\ndriver.dio.device[0].pdocnt' | nc -4 127.0.0.1 4407
    Nspd
    file
    fatal

    19.2 fatal on trying to get more than one file data in once request:
    $ echo -ne 'Nspd\x26\x0\x0\x00file\r\ndriver.dio\r\nmotion.navigation' | nc -4 127.0.0.1 4407
    Nspd
    file
    fatal

    19.3 normal request:
    $ echo -ne 'Nspd\x10\x0\x0\x00file\r\ndriver.dio' | nc -4 127.0.0.1 4407
    Nspdv
    file
    ok
    {
        "device":   [{
                "id":   "199",
                "name": "red",
                "can":  "101",
                "port": "0",
                "node": "35",
                "latency":  "0",
                "merge":    "0",
                "di_channel_num":   "0",
                "do_channel_num":   "0",
                "ais":  {
                    "block":    [{
                            "start_address":    "0x2170",
                            "effective":    "8",
                            "internel_type":    "16"
                        }]
                },
                "aos":  {
                    "block":    [{
                            "start_address":    "0x000",
                            "effective":    "8",
                            "internel_type":    "3"
                        }]
                }
            }, {
                "id":   "198",
                "name": "dio",
                "can":  "101",
                "port": "0",
                "node": "16",
                "latency":  "0",
                "merge":    "1",
                "di_channel_num":   "1",
                "do_channel_num":   "1",
                "ais":  {
                    "block":    [{
                            "start_address":    "0x2101",
                            "effective":    "0",
                            "internel_type":    "32"
                        }]
                },
                "aos":  {
                    "block":    [{
                            "start_address":    "0x00",
                            "effective":    "8",
                            "internel_type":    "3"
                        }]
                }
            }]
        }


20. test case for subscribe/publish, the publish message sent only when value string has been changed
    20.1 subscribe for one peer, it will received publish message
    $ echo -ne 'Nspd\x11\x0\x0\x0subscribe\r\ndriver' | nc -4 127.0.0.1 4407
    Nspd
    subscribe
    ok
    NspdM
    publish
    driver.dio.device[0].ais.block[0].start_address
    driver.dio.name
    Nspd
    publish
    driver.dio.name

    20.2 change config by other peer
    $ echo -ne 'Nspd\x53\x0\x0\x0write\r\ndriver.dio.device[0].ais.block[0].start_address=0x2180\r\ndriver.dio.name=left' | nc -4 127.0.0.1 4407
    Nspd
    write
    ok
    ok
    $ echo -ne 'Nspd\x53\x0\x0\x0write\r\ndriver.dio.device[0].ais.block[0].start_address=0x2180\r\ndriver.dio.name=bash' | nc -4 127.0.0.1 4407
    Nspd
    write
    ok
    ok

    20.3 caller can use wildcard '/' to subscribe all changes
    $ echo -ne 'Nspd\x0c\x0\x0\x0subscribe\r\n/' | nc -4 127.0.0.1 4407
    Nspd
    subscribe
    ok

21. test case for binary write/read
    21.1 write binary to jess
    $ echo -ne 'Nspd\x25\x0\x0\x0wbin\r\noffset=0\r\nsize=10\r\n\r\nabcd#1234?' | nc -4 127.0.0.1 4407
    Nspd!
    wbin
    offset=0
    size=10

    ok

    $ hexdump -s 1024 -n 10 /var/jess/binary.db
    0000400 6261 6463 3123 3332 3f34
    000040a

    21.2 read binary from jess
    $ echo -ne 'Nspd\x1B\x0\x0\x0rbin\r\noffset=0\r\nsize=10\r\n\r\n' | nc -4 127.0.0.1 4407
    Nspd+
    rbin
    offset=0
    size=10

    ok
    abcd#1234?

    21.3 read binary form jess with specify offset
    $ echo -ne 'Nspd\x1A\x0\x0\x0rbin\r\noffset=2\r\nsize=5\r\n\r\n' | nc -4 127.0.0.1 4407
    Nspd%
    rbin
    offset=2
    size=5

    ok
    cd#12

22.  test case for mkdir
    $ echo -ne 'Nspd\x0B\x0\x0\x0mkdir\r\njess' | nc -4 127.0.0.1 4407
    Nspd
    mkdir
    ok

23.  test case for rmdir
    23.1 normal
    $ echo -ne 'Nspd\x0B\x0\x0\x0rmdir\r\njess' | nc -4 127.0.0.1 4407
    Nspd
    rmdir
    ok

    23.2 rmdir can use wilcard '/' to remove all contain directorys and files in current database set
    $ easyjess rmdir / 127.0.0.1
    rmdir
    ok

24. test cast for unsupport protocol
    $ echo -ne 'Nspd\x10\x0\x0\x00shit\r\ndriver.dio' | nc -4 127.0.0.1 4407
    Nspd
    shit
    fatal

    $ echo -ne 'Nspd\x27\x0\x0\x00shit\r\nid=100\r\nname=wahaha\r\n\r\nndriver.dio' | nc -4 127.0.0.1 4407
    Nspd&
    shit
    id=100
    name=wahaha

    fatal

25. test case for keepalive
    $ echo -ne 'Nspd\x0B\x0\x0\x00keepalive\r\n' | nc -4 127.0.0.1 4407
    Nspd
    keepalive

26. user can discover the jess server by using UDP unicast requenst, any data in request packet should return the same response by jess server
    $ echo -ne 'Nspd\x04\x0\x0\x0test' | nc -u 127.0.0.1 4407
    Nspdtest

27. the login request detail instruction(algorithm description):
    generate 4 bytes random integer @seed,
    use vfn1a_h32 algorithm function to encode @seed into a 32 bit unsigned integer @evfn
    divide @evfn into 4 single bytes order by little-endian
    merge @seed and @evfn into a 8 bytes array @enc by the following rule:
    enc[0] = seed[0]
    enc[1] = evfn[0]
    enc[2] = seed[1]
    enc[3] = evfn[1]
    enc[4] = seed[2]
    enc[5] = evfn[2]
    enc[6] = seed[3]
    enc[7] = evfn[3]
    encode @enc into a visual string buffer by base64 algorithm
    this buffer can use to acquire jess login now.
    $ echo -ne 'Nspd\x13\x0\x0\x0login\r\neGo0ZYmG7rA=' | nc -4 127.0.0.1 4407
    Nspd
    login
    ok
    $ echo -ne 'Nspd\x13\x0\x0\x0login\r\neGo0qYmG1rA=' | nc -4 127.0.0.1 4407
    Nspd
    login
    fatal
28.  test case for create checkpoint(ignore the '/' parameter in request command):
    Notes: proc directory NOT real exist, it only in memory, so, file(19) command will catch a error
    $ easyjess checkpoint / 127.0.0.1
    checkpoint
    ok
    >>>>>>>>> jess consume 2.669 ms.
    $ easyjess scope proc.jess 127.0.0.1
    scope
    ok
    jess.checkpoint[0]=jess_checkpoint_20190816_105539
    >>>>>>>>> jess consume 0.458 ms.
    $ easyjess checkpoint / 127.0.0.1
    checkpoint
    ok
    >>>>>>>>> jess consume 2.323 ms.
    $ easyjess scope proc.jess 127.0.0.1
    scope
    ok
    jess.checkpoint[0]=jess_checkpoint_20190816_105539
    jess.checkpoint[1]=jess_checkpoint_20190816_105544
29.  test case for recover
    29.1 original status
    $ easyjess contain / 127.0.0.1
    contain
    ok
    foundation
    driver
    custom
    embedded
    motion
    map
    localization
    test
    proc
    unknow
    >>>>>>>>> jess consume 0.523 ms.
    29.2 remove all data
    $ easyjess rmdir / 127.0.0.1
    rmdir
    ok
    >>>>>>>>> jess consume 7.828 ms.
    $ easyjess contain / 127.0.0.1
    contain
    ok
    >>>>>>>>> jess consume 0.381 ms.
    29.3 recover by a checkpoint
    $ easyjess recover jess_checkpoint_20190816_141929 127.0.0.1
    recover
    ok
    >>>>>>>>> jess consume 105.732 ms.
    29.4 after recover
    $ easyjess contain / 127.0.0.1
    contain
    ok
    foundation
    driver
    custom
    embedded
    motion
    map
    localization
    test
    proc
    unknow
    >>>>>>>>> jess consume 0.309 ms.
30. test case for subscribe
    subscribe foundation 127.0.0.1
31. demo for unsubscribe
    unsubscribe foundation 127.0.0.1
    unsubscribe * 127.0.0.1     # tihs command can remove all subscribed item of current session
