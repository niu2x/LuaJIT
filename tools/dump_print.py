import os
import sys
import argparse
import datetime
import time

def die(err):
	print(err)
	raise "lexer error"

class Lexer: 
	def __init__(self, file):
		self.file = file
		self.state = 'lj_gc_dump: total'

		self.objs = {}

		self.mainthread = None
		self.mainthread_env = None
		self.registrytv = None
		self.gc_root = []

	def run(self):
		self.read_sz('lj_gc_dump: total ')
		total = int(self.read_until('\n'))
		self.read_sz('\n')
		print("Total: ", total)

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

		assert(self.file.read(1) == b'')

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

			while self.read_sz(' ' * (deep+2) + 'stack: \n', False):
				o = self.read_obj(deep+2)
				obj['stack'].append(o)


			self.read_sz(' ' * (deep+2) + 'env: \n')
			obj['env'] = self.read_obj(deep+2)
			return addr

		elif self.read_sz('Func[', False):
			addr = self.read_until(']')
			self.read_sz(']\n')
			obj = {'type': "Fun", 'uv':[]}
			self.objs[addr] = obj

			self.read_sz(' ' * (deep+2))
			self.read_sz('env: \n')
			env = self.read_obj(deep+2)
			obj['env'] = env

			if self.read_sz(' ' * (deep+2) + 'proto: \n', False):
				obj['proto'] = self.read_obj(deep+2)

			while self.read_sz(' ' * (deep+2) + 'uv: \n', False):
				uv = self.read_obj(deep+2)
				obj['uv'].append(uv)

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
			return addr
		elif self.read_sz('STR[', False):
			addr = self.read_until(',')
			self.read_until(']')
			self.read_sz(']: (')
			strlen = int(self.read_until(')'))
			self.read_sz(') ')
			sz = self.file.read(strlen)
			self.objs[addr] = {'type': "STR"}
			self.read_sz('\n')
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
			return addr		
		elif self.read_sz('Non-gc obj\n', False):
			return 'Non-gc'
		elif self.read_sz('Proto[', False):
			addr = self.read_until(',')
			self.read_until('\n')
			self.read_sz('\n')

			obj = {'type': 'Proto'}
			self.read_sz((deep+2)*' ' + "chunkname: \n")
			obj['chunkname'] = self.read_obj(deep+2)

			self.objs[addr] = obj
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
					die(f'unexpect {chr(cc)}, expect {chr(c)}')
				self.file.seek(pos)
				return False
		return True

	def read_until(self, sep):
		sep = sep.encode('utf-8')
		assert(len(sep) == 1)
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


def subcommand_summary(args):
	with open(args.file, "rb") as f:
		lexer = Lexer(f)
		lexer.run()
		lexer.summary()


def parse_args():
    parser = argparse.ArgumentParser(description="")
    subparsers = parser.add_subparsers()
    # 添加greet子命令
    greet_parser = subparsers.add_parser(
        "summary", help="",
        description='',
        formatter_class=argparse.RawTextHelpFormatter)
    greet_parser.add_argument('-f', '--file', type=str, help='', required=True)
    greet_parser.set_defaults(func=subcommand_summary)
    
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