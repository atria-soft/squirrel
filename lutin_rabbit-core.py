#!/usr/bin/python
import lutin.debug as debug
import lutin.tools as tools
import os


def get_type():
	return "LIBRARY"

def get_desc():
	return "rabbit script interpreter"

def get_licence():
	return "MIT"

def get_compagny_type():
	return "org"

def get_compagny_name():
	return "rabbit"

def get_maintainer():
	return ["Edouard DUPIN <edupin@gmail.com>"]

def get_version():
	return [0,1]

def configure(target, my_module):
	my_module.add_src_file([
		'rabbit/sqmem.cpp',
		'rabbit/sqbaselib.cpp',
		'rabbit/sqapi.cpp',
		'rabbit/sqlexer.cpp',
		'rabbit/sqclass.cpp',
		'rabbit/AutoDec.cpp',
		'rabbit/VirtualMachine.cpp',
		'rabbit/sqtable.cpp',
		'rabbit/sqstate.cpp',
		'rabbit/sqobject.cpp',
		'rabbit/sqcompiler.cpp',
		'rabbit/sqdebug.cpp',
		'rabbit/sqfuncstate.cpp',
		'rabbit/RefCounted.cpp',
		'rabbit/WeakRef.cpp',
		])
	my_module.compile_version("c++", 2011)
	my_module.add_depend([
		'z',
		'm',
		'c',
		'etk-base',
		])
	my_module.add_header_file([
		'rabbit/sqclass.hpp',
		'rabbit/AutoDec.hpp',
		'rabbit/VirtualMachine.hpp',
		'rabbit/sqstate.hpp',
		'rabbit/rabbit.hpp',
		'rabbit/sqobject.hpp',
		'rabbit/sqopcodes.hpp',
		'rabbit/UserData.hpp',
		'rabbit/squtils.hpp',
		'rabbit/sqpcheader.hpp',
		'rabbit/sqfuncproto.hpp',
		'rabbit/sqconfig.hpp',
		'rabbit/sqcompiler.hpp',
		'rabbit/Array.hpp',
		'rabbit/sqclosure.hpp',
		'rabbit/sqlexer.hpp',
		'rabbit/sqfuncstate.hpp',
		'rabbit/sqstring.hpp',
		'rabbit/sqtable.hpp',
		'rabbit/RefCounted.hpp',
		'rabbit/WeakRef.hpp',
		])
	return True


