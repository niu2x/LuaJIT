import os
import sys
import argparse
import datetime
import time
import pickle

def die(err):
    print(err)
    raise "lexer error"

class Lexer: 
    def __init__(self, file):
        self.file = file

        self.objs = {}
        self.mainthread = None
        self.mainthread_env = None
        self.registrytv = None
        self.gc_root = []

    def save(self, path):
        data_to_serialize = {
            "objs": self.objs,
            "mainthread": self.mainthread,
            "mainthread_env": self.mainthread_env,
            "registrytv": self.registrytv,
            "gc_root": self.gc_root,
        }

        # 将字典序列化并保存到文件
        with open(path, "wb") as f:
            pickle.dump(data_to_serialize, f)

    def load(self, path):
        with open(path, 'rb') as f:
            data = pickle.load(f)
            self.objs = data['objs']
            self.mainthread = data['mainthread']
            self.mainthread_env = data['mainthread_env']
            self.registrytv = data['registrytv']
            self.gc_root = data['gc_root']

    def run(self):
        self.read_sz('lj_gc_dump: total ')
        total = int(self.read_until('\n'))
        self.read_sz('\n')

        while self.read_sz('STR[', False):
            addr = self.read_until(',')
            self.read_sz(',')
            self.read_until('(')
            self.read_sz('(')
            strlen = int(self.read_until(')'))
            self.read_sz(') ')
            sz = self.file.read(strlen)
            self.read_sz('\n')

        self.read_sz('mainthread: \n')
        self.mainthread = self.read_obj(0)

        self.read_sz('mainthread_env: \n')
        self.mainthread_env = self.read_obj(0)

        self.read_sz('registrytv: \n')
        self.registrytv = self.read_obj(0)

        while self.read_sz('gc_root_'):
            self.read_until('\n')
            self.read_sz('\n')
            self.gc_root.append(self.read_obj(0))

        assert self.file.read(1) == b''

    def read_objs(self, deep):
        objs = []
        while True:
            obj = self.read_obj(deep)
            if obj:
                objs.append(obj)
            else:
                break
        return objs

    def read_obj(self, deep):
        if not self.read_sz(' ' * deep, False):
            return
        if self.read_sz('Thread[', False):
            addr = self.read_until(']')
            self.read_sz(']\n')
            obj = {'type': "Thread", "stack":[]}
            self.objs[addr] = obj
            obj['addr'] = addr

            while self.read_sz(' ' * (deep+2) + 'stack: \n', False):
                o = self.read_obj(deep+2)
                obj['stack'].append(o)

            self.read_sz(' ' * (deep+2) + 'env: \n')
            obj['env'] = self.read_obj(deep+2)
            return addr

        elif self.read_sz('Func[', False):
            addr = self.read_until(']')
            self.read_sz(']\n')
            obj = {'type': "Func", 'uv':[]}
            self.objs[addr] = obj

            self.read_sz(' ' * (deep+2))
            self.read_sz('env: \n')
            env = self.read_obj(deep+2)
            obj['env'] = env

            if self.read_sz(' ' * (deep+2) + 'proto: \n', False):
                obj['proto'] = self.read_obj(deep+2)
                obj['type'] = "Lua-Func"
            else:
                obj['type'] = "C-Func"

            while self.read_sz(' ' * (deep+2) + 'uv: \n', False):
                uv = self.read_obj(deep+2)
                obj['uv'].append(uv)

            obj['addr'] = addr
            return addr
        elif self.read_sz('TAB[', False):

            addr = self.read_until(']')
            self.read_sz(']\n')
            obj = {'type': "Tab", 'hashpart':{} , 'arraypart':[] }
            self.objs[addr] = obj

            if self.read_sz(' ' * (deep+2) + 'metatable: \n', False):
                obj['meta'] = self.read_obj(deep+2)

            while True:
                if self.read_sz(' ' * (deep+2) + 'hashpart_key: \n', False):
                    key = self.read_obj(deep+2)
                    self.read_sz(' ' * (deep+2) + 'hashpart_value: \n')
                    value = self.read_obj(deep+2)
                    obj['hashpart'][key] = value
                elif self.read_sz(' ' * (deep+2) + 'array_part: \n', False):
                    value = self.read_obj(deep+2)
                    obj['arraypart'].append(value)
                else:
                    break
            obj['addr'] = addr
            return addr
        elif self.read_sz('STR[', False):
            addr = self.read_until(',')
            self.read_until(']')
            self.read_sz(']: (')
            strlen = int(self.read_until(')'))
            self.read_sz(') ')
            sz = self.file.read(strlen)
            obj = {'type': "STR"}
            obj['sz'] = sz
            self.objs[addr] = obj
            self.read_sz('\n')
            obj['addr'] = addr
            return addr
        elif self.read_sz('already dump[', False):
            addr = self.read_until(']')
            self.read_sz(']\n')
            return addr 
        elif self.read_sz('udata[', False): 
            addr = self.read_until(']')
            self.read_sz(']\n')
            obj = {'type': "udata"}
            if self.read_sz((deep+2)*' ' + 'meta: \n', False):
                obj['meta'] = self.read_obj(deep + 2)       

            self.objs[addr] = obj
            obj['addr'] = addr
            return addr     
        elif self.read_sz('Non-gc obj\n', False):
            return 'Non-gc'
        elif self.read_sz('Proto[', False):
            addr = self.read_until(',')
            self.read_sz(', firstline: ')
            firstline = int (self.read_until(']'))
            self.read_until('\n')
            self.read_sz('\n')

            obj = {'type': 'Proto', 'kgc':[], 'firstline': firstline}
            self.read_sz((deep+2)*' ' + "chunkname: \n")
            obj['chunkname'] = self.read_obj(deep+2)


            while self.read_sz(' ' * (deep+2) + 'kgc: \n', False):
                kgc = self.read_obj(deep+2)
                obj['kgc'].append(kgc)

            self.objs[addr] = obj
            obj['addr'] = addr
            return addr

        elif self.read_sz('UV[', False):
            addr = self.read_until(']')
            self.read_sz(']\n')
            self.read_sz((deep+2)*' ' + "uv_content: \n")
            obj = {'type': 'UV', 'content': self.read_obj(deep+2)}

            self.objs[addr] = obj
            obj['addr'] = addr
            return addr

        else:
            die(f"unsupport obj {self.read_until(']')}")
            pass

    def read_sz(self, sz, must=True):
        pos = self.file.tell()
        sz = sz.encode('utf-8')
        for i in range(len(sz)):
            c = sz[i]
            cc = self.file.read(1);
            if len(cc) != 1:
                return False
            cc = cc[0]
            if c != cc:
                if must:
                    die(f'unexpect {chr(cc)}, expect {chr(c)} next is {self.file.read(128)}')
                self.file.seek(pos)
                return False
        return True

    def read_until(self, sep):
        sep = sep.encode('utf-8')
        assert len(sep) == 1
        buf = b''
        while True:
            c = self.file.read(1)
            if c[0] == sep[0]:
                self.file.seek(self.file.tell() - 1)
                return buf
            else:
                buf = buf + c

    def summary(self):
        counter = {}
        for addr in self.objs:
            obj = self.objs[addr]
            if obj['type'] in counter:
                counter[obj['type']] += 1
            else:
                counter[obj['type']] = 1

        for t in counter:
            print(t, counter[t])

    def show_objs(self):
        for addr in self.objs:
            obj = self.objs[addr]
            print(obj['type'], addr, self.str_obj(addr))

    def why_alive(self, addr):
        addr = addr.encode('utf-8')
        visited = {}
        path = self.search(self.mainthread, addr, ['mainthread'], visited)
        if path:
            return path

        path = self.search(self.mainthread_env, addr, ['mainthread_env'], visited)
        if path:
            return path

        path = self.search(self.registrytv, addr, ['registrytv'], visited)
        if path:
            return path

        for v in self.gc_root:
            path = self.search(v, addr, ['gc_root'], visited)
            if path:
                return path

        return []

    def str_obj(self, addr):
        if addr == 'Non-gc':
            return "Non-gc"
        obj = self.objs[addr]
        if obj['type'] == 'STR':
            try:
                return "STR[" + obj['sz'].decode('utf-8') + ']'
            except:
                return "STR[" + addr.decode('utf-8') + ']'
        elif obj['type'] == 'Proto':
            chunkname = self.str_obj(obj['chunkname'])
            firstline = obj['firstline']
            return f'Proto[{chunkname} {firstline}]'
        elif obj['type'] == 'Lua-Func':
            proto = obj['proto']
            return f'Lua-Func[{self.str_obj(proto)}]'
        else:
            return obj['type'] + '[]'

    def search(self, obj_addr, addr, parent_path, visited):
        if obj_addr in visited:
            return 

        if obj_addr == 'Non-gc':
            return

        visited[obj_addr] = True

        if obj_addr == addr:
            return parent_path

        obj = self.objs[obj_addr]

        if obj['type'] == 'Thread':
            for s in obj['stack']:
                path = self.search(s, addr, parent_path + ['Thread[STACK]'], visited)
                if path:
                    return path
            s = obj['env']
            path = self.search(s, addr, parent_path + ['Thread[ENV]'], visited)
            if path:
                return path
        elif obj['type'] == 'UV':
            s = obj['content']
            path = self.search(s, addr, parent_path + ['UV[content]'], visited)
            if path:
                return path
        elif obj['type'] == 'Lua-Func' or obj['type'] == 'C-Func' :
            s = obj['env']
            path = self.search(s, addr, parent_path + [obj['type'] + '[env]'], visited)
            if path:
                return path

            if obj['type'] == 'Lua-Func':
                s = obj['proto']
                path = self.search(s, addr, parent_path + ['Func[proto]'], visited)
                if path:
                    return path

            for s in obj['uv']:
                path = self.search(s, addr, parent_path + ['Func[uv]'], visited)
                if path:
                    return path

        elif obj['type'] == 'Tab':
            for s in obj['arraypart']:
                path = self.search(s, addr, parent_path + ['Tab[arraypart]'], visited)
                if path:
                    return path

            for k in obj['hashpart']:
                v = obj['hashpart'][k]


                key_str = self.str_obj(k)

                path = self.search(k, addr, parent_path + [f'Tab[hashpart KEY:{key_str} key]'], visited)
                if path:
                    return path

                path = self.search(v, addr, parent_path + [f'Tab[hashpart KEY:{key_str} value]'], visited)
                if path:
                    return path

            if 'meta' in obj:
                s = obj['meta']
                path = self.search(s, addr, parent_path + ['Tab[meta]'], visited)
                if path:
                    return path


        elif obj['type'] == 'Proto':
            for s in obj['kgc']:
                path = self.search(s, addr, parent_path + ['Proto[kgc]'], visited)
                if path:
                    return path


            s = obj['chunkname']
            path = self.search(s, addr, parent_path + ['Proto[chunkname]'], visited)
            if path:
                return path

        elif obj['type'] == 'udata':
            if 'meta' in obj:
                s = obj['meta']
                path = self.search(s, addr, parent_path + ['udata[meta]'], visited)
                if path:
                    return path

        elif obj['type'] == 'STR':
            pass

        else:
            assert False, f"unsupport type: {obj['type']}"


def subcommand_cache(args):
    with open(args.file, "rb") as f:
        lexer = Lexer(f)
        lexer.run()
        lexer.save(args.cache)

def subcommand_summary(args):
    lexer = Lexer(None)
    lexer.load(args.cache)
    lexer.summary()

def subcommand_show(args):
    lexer = Lexer(None)
    lexer.load(args.cache)
    lexer.show_objs()

def subcommand_why_alive(args):
    lexer = Lexer(None)
    lexer.load(args.cache)
    print('->\n'.join(lexer.why_alive(args.addr)))

def parse_args():
    parser = argparse.ArgumentParser(description="")
    subparsers = parser.add_subparsers()
    # 添加greet子命令
    greet_parser = subparsers.add_parser(
        "summary", help="",
        description='',
        formatter_class=argparse.RawTextHelpFormatter)
    greet_parser.add_argument('-c', '--cache', type=str, help='filename', required=True)
    greet_parser.set_defaults(func=subcommand_summary)


    greet_parser = subparsers.add_parser(
        "cache", help="",
        description='',
        formatter_class=argparse.RawTextHelpFormatter)
    greet_parser.add_argument('-f', '--file', type=str, help='filename', required=True)
    greet_parser.add_argument('-c', '--cache', type=str, help='filename', required=True)
    greet_parser.set_defaults(func=subcommand_cache)



    greet_parser = subparsers.add_parser(
        "show", help="",
        description='',
        formatter_class=argparse.RawTextHelpFormatter)
    greet_parser.add_argument('-c', '--cache', type=str, help='filename', required=True)
    greet_parser.set_defaults(func=subcommand_show)

    greet_parser = subparsers.add_parser(
        "why_alive", help="",
        description='',
        formatter_class=argparse.RawTextHelpFormatter)
    greet_parser.add_argument('-c', '--cache', type=str, help='filename', required=True)
    greet_parser.add_argument('-a', '--addr', type=str, help='', required=True)
    greet_parser.set_defaults(func=subcommand_why_alive)

    
    args = parser.parse_args()

    if not hasattr(args, "func"):
        parser.print_help()
        sys.exit(1)
    return args


def main():

    args = parse_args()
    args.func(args)

if __name__ == "__main__":
    main()