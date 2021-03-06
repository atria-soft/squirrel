/**
 * @author Alberto DEMICHELIS
 * @author Edouard DUPIN
 * @copyright 2018, Edouard DUPIN, all right reserved
 * @copyright 2003-2017, Alberto DEMICHELIS, all right reserved
 * @license MPL-2 (see license file)
 */
#pragma once

#include <etk/types.hpp>
#include <rabbit/Hash.hpp>
#include <rabbit/RefCounted.hpp>
#include <rabbit/sqconfig.hpp>

namespace rabbit {
	class SharedState;
	class ObjectPtr;
	
	rabbit::Hash _hashstr (const char *s, size_t l);
	
	class String : public rabbit::RefCounted {
		public:
			String(){}
			~String(){}
		public:
			static rabbit::String *create(rabbit::SharedState *ss, const char *, int64_t len = -1 );
			int64_t next(const rabbit::ObjectPtr &refpos, rabbit::ObjectPtr &outkey, rabbit::ObjectPtr &outval);
			void release();
			rabbit::SharedState *_sharedstate;
			rabbit::String *_next; //chain for the string table
			int64_t _len;
			rabbit::Hash _hash;
			char _val[1];
	};


	
}

