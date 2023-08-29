import subprocess
from shutil import rmtree
from time import sleep
import multiprocessing

"""
create table t (id int);
insert into t values (1);
"""

N = 1000     # 数据量
n = 4       # 线程数


try:
    rmtree('test')
except:
    print('failed to rmdir test')

with subprocess.Popen(['./build/bin/rmdb', 'test']) as rmdb:
    print(f'@@@ Open rmdb, pid: {rmdb.pid}')

    sleep(0.5)

    client = []
    for i in range(n):
        client.append(subprocess.Popen(['./build/bin/rmdb_client'], stdin=subprocess.PIPE, text=True))
        print(f'@@@ Open client{i}, pid: {client[-1].pid}')

    client[0].stdin.write('create table t (id int);\n')
    sleep(0.2)

    for i in range(N):
        for c in client:
            c.stdin.write(f'insert into t values ({i});\n')

    for i in range(N, N * 2):
        for c in client:
            c.stdin.write(f'insert into t values ({i});\n')

    for i in range(N):
        for c in client:
            c.stdin.write(f'delete from t where id={i};\n')

    client[0].stdin.write(f'select count(*) as cnt_id from t;\n')
    client[0].stdin.write(f'select min(*) as min_id from t;\n')
    client[0].stdin.write(f'select max(*) as max_id from t;\n')


    for i, c in enumerate(client):
        c.communicate('exit;\n')
        c.terminate()
        print(f'@@@ terminate client{i}')

    print('@@@ terminate rmdb')
    rmdb.kill()

with subprocess.Popen(['./build/bin/rmdb', 'test']) as rmdb:
    print(f'@@@ Open rmdb, pid: {rmdb.pid}')

    sleep(1)
    with subprocess.Popen(['./build/bin/rmdb_client'], stdin=subprocess.PIPE, text=True) as client:
        print(f'@@@ Open client{i}, pid: {client.pid}')

        client.stdin.write(f'select count(*) as cnt_id from t;\n')
        client.stdin.write(f'select min(*) as min_id from t;\n')
        client.stdin.write(f'select max(*) as max_id from t;\n')

        client.communicate('exit;\n')
    rmdb.terminate()