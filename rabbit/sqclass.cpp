/**
 * @author Alberto DEMICHELIS
 * @author Edouard DUPIN
 * @copyright 2018, Edouard DUPIN, all right reserved
 * @copyright 2003-2017, Alberto DEMICHELIS, all right reserved
 * @license MPL-2 (see license file)
 */
#include <rabbit/sqpcheader.hpp>
#include <rabbit/VirtualMachine.hpp>
#include <rabbit/sqtable.hpp>
#include <rabbit/sqclass.hpp>
#include <rabbit/sqfuncproto.hpp>
#include <rabbit/sqclosure.hpp>
#include <rabbit/MetaMethod.hpp>


SQClass::SQClass(SQSharedState *ss,SQClass *base)
{
	_base = base;
	_typetag = 0;
	_hook = NULL;
	_udsize = 0;
	_locked = false;
	_constructoridx = -1;
	if(_base) {
		_constructoridx = _base->_constructoridx;
		_udsize = _base->_udsize;
		_defaultvalues = base->_defaultvalues;
		_methods = base->_methods;
		_COPY_VECTOR(_metamethods,base->_metamethods, rabbit::MT_LAST);
		__ObjaddRef(_base);
	}
	_members = base?base->_members->clone() : SQTable::create(ss,0);
	__ObjaddRef(_members);
}

void SQClass::finalize() {
	_attributes.Null();
	_NULL_SQOBJECT_VECTOR(_defaultvalues,_defaultvalues.size());
	_methods.resize(0);
	_NULL_SQOBJECT_VECTOR(_metamethods, rabbit::MT_LAST);
	__Objrelease(_members);
	if(_base) {
		__Objrelease(_base);
	}
}

SQClass::~SQClass()
{
	finalize();
}

bool SQClass::newSlot(SQSharedState *ss,const rabbit::ObjectPtr &key,const rabbit::ObjectPtr &val,bool bstatic)
{
	rabbit::ObjectPtr temp;
	bool belongs_to_static_table =    sq_type(val) == rabbit::OT_CLOSURE
	                               || sq_type(val) == rabbit::OT_NATIVECLOSURE
	                               || bstatic;
	if(_locked && !belongs_to_static_table)
		return false; //the class already has an instance so cannot be modified
	if(_members->get(key,temp) && _isfield(temp)) //overrides the default value
	{
		_defaultvalues[_member_idx(temp)].val = val;
		return true;
	}
	if(belongs_to_static_table) {
		int64_t mmidx;
		if(    (    sq_type(val) == rabbit::OT_CLOSURE
		         || sq_type(val) == rabbit::OT_NATIVECLOSURE )
		    && (mmidx = ss->getMetaMethodIdxByName(key)) != -1) {
			_metamethods[mmidx] = val;
		}
		else {
			rabbit::ObjectPtr theval = val;
			if(_base && sq_type(val) == rabbit::OT_CLOSURE) {
				theval = _closure(val)->clone();
				_closure(theval)->_base = _base;
				__ObjaddRef(_base); //ref for the closure
			}
			if(sq_type(temp) == rabbit::OT_NULL) {
				bool isconstructor;
				rabbit::VirtualMachine::isEqual(ss->_constructoridx, key, isconstructor);
				if(isconstructor) {
					_constructoridx = (int64_t)_methods.size();
				}
				SQClassMember m;
				m.val = theval;
				_members->newSlot(key,rabbit::ObjectPtr(_make_method_idx(_methods.size())));
				_methods.pushBack(m);
			}
			else {
				_methods[_member_idx(temp)].val = theval;
			}
		}
		return true;
	}
	SQClassMember m;
	m.val = val;
	_members->newSlot(key,rabbit::ObjectPtr(_make_field_idx(_defaultvalues.size())));
	_defaultvalues.pushBack(m);
	return true;
}

SQInstance *SQClass::createInstance()
{
	if(!_locked) lock();
	return SQInstance::create(NULL,this);
}

int64_t SQClass::next(const rabbit::ObjectPtr &refpos, rabbit::ObjectPtr &outkey, rabbit::ObjectPtr &outval)
{
	rabbit::ObjectPtr oval;
	int64_t idx = _members->next(false,refpos,outkey,oval);
	if(idx != -1) {
		if(_ismethod(oval)) {
			outval = _methods[_member_idx(oval)].val;
		}
		else {
			rabbit::ObjectPtr &o = _defaultvalues[_member_idx(oval)].val;
			outval = _realval(o);
		}
	}
	return idx;
}

bool SQClass::setAttributes(const rabbit::ObjectPtr &key,const rabbit::ObjectPtr &val)
{
	rabbit::ObjectPtr idx;
	if(_members->get(key,idx)) {
		if(_isfield(idx))
			_defaultvalues[_member_idx(idx)].attrs = val;
		else
			_methods[_member_idx(idx)].attrs = val;
		return true;
	}
	return false;
}

bool SQClass::getAttributes(const rabbit::ObjectPtr &key,rabbit::ObjectPtr &outval)
{
	rabbit::ObjectPtr idx;
	if(_members->get(key,idx)) {
		outval = (_isfield(idx)?_defaultvalues[_member_idx(idx)].attrs:_methods[_member_idx(idx)].attrs);
		return true;
	}
	return false;
}

///////////////////////////////////////////////////////////////////////
void SQInstance::init(SQSharedState *ss)
{
	_userpointer = NULL;
	_hook = NULL;
	__ObjaddRef(_class);
	_delegate = _class->_members;
}

SQInstance::SQInstance(SQSharedState *ss, SQClass *c, int64_t memsize)
{
	_memsize = memsize;
	_class = c;
	uint64_t nvalues = _class->_defaultvalues.size();
	for(uint64_t n = 0; n < nvalues; n++) {
		new (&_values[n]) rabbit::ObjectPtr(_class->_defaultvalues[n].val);
	}
	init(ss);
}

SQInstance::SQInstance(SQSharedState *ss, SQInstance *i, int64_t memsize)
{
	_memsize = memsize;
	_class = i->_class;
	uint64_t nvalues = _class->_defaultvalues.size();
	for(uint64_t n = 0; n < nvalues; n++) {
		new (&_values[n]) rabbit::ObjectPtr(i->_values[n]);
	}
	init(ss);
}

void SQInstance::finalize()
{
	uint64_t nvalues = _class->_defaultvalues.size();
	__Objrelease(_class);
	_NULL_SQOBJECT_VECTOR(_values,nvalues);
}

SQInstance::~SQInstance()
{
	if(_class){ finalize(); } //if _class is null it was already finalized by the GC
}

bool SQInstance::getMetaMethod(rabbit::VirtualMachine* SQ_UNUSED_ARG(v),rabbit::MetaMethod mm,rabbit::ObjectPtr &res)
{
	if(sq_type(_class->_metamethods[mm]) != rabbit::OT_NULL) {
		res = _class->_metamethods[mm];
		return true;
	}
	return false;
}

bool SQInstance::instanceOf(SQClass *trg)
{
	SQClass *parent = _class;
	while(parent != NULL) {
		if(parent == trg)
			return true;
		parent = parent->_base;
	}
	return false;
}
