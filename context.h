// Copyright 2018 authors. All Rights Reserved.
// Author: allen(ivan_allen@qq.com)
//
// context 
#pragma once

#include <iostream>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <memory>
#include "chan.h"

enum class ContextType {
    CANCEL = 0,
    TIMER,
    VALUE,
    NONE,
};

class Context_ {
public:
    Context_(const std::shared_ptr<Context_>& parent): _parent(parent) {}
    virtual ~Context_() {std::cout << "~Context()" << std::endl;}
	virtual std::string value(const std::string& key) const = 0;
	virtual int done() const = 0;
    virtual ContextType type() const = 0;

    std::shared_ptr<Context_> _parent;
};

using Context=std::shared_ptr<Context_>;

class EmptyContext: public Context_ {
public:
    EmptyContext(const Context& parent): Context_(parent) {}
	std::string value(const std::string& key) const override {
		return std::string();
	}

	int done() const override {
		return 0;
	}

    ContextType type() const override {
        return ContextType::NONE;
    }
};

class ValueContext: public Context_ {
public:
	ValueContext(const Context& parent, const std::string& k, const std::string& v):
		Context_(parent),
   		_key(k),
		_value(v) {}

	std::string value(const std::string& key) const override {
		if (_key != key)
			return _parent->value(key);
		else
			return _value;
	}
	int done() const override {
		return _parent->done();
	}
    ContextType type() const override {
        return ContextType::VALUE;
    }

private:
	std::map<std::string, std::string> _kv;
	std::string _key;
	std::string _value;

	//Context _parent;
};

class CancelContext: public std::enable_shared_from_this<CancelContext>, public Context_ {
public:
    CancelContext(const Context& parent):
        Context_(parent),
        _done(make_chan<int>()){
    }

    ~CancelContext() {
        if (_th.joinable()) {
            _th.join();
        }
    }

	std::string value(const std::string& key) const override {
		return _parent->value(key);
	}

	int done() const override {
		return _done->rfd();
	}

    ContextType type() const override {
        return ContextType::CANCEL;
    }

    void cancel() {
        std::lock_guard<std::mutex> lk(_mu);
        _done->close();
        for(auto& child : _children) {
            child->_done->close();
        }
        _children.clear();
    }
    void init() {
        auto p = parent_cancel_context(this->_parent);
        auto self = shared_from_this();
        if (p == nullptr) {
            _th = std::thread([self]{
                if (self->_parent->done()) {
                    auto active = choose({self->_parent->done(), self->done()});
                    if (active[0]) {
                        self->cancel();
                    }
                } else {
                    auto active = choose({self->done()});
                }
            });
        } else {
            std::lock_guard<std::mutex> lk(_mu);
            p->_children.push_back(self);
        }
    }

private:
    std::shared_ptr<CancelContext> parent_cancel_context(const Context& ctx) {
        auto p = ctx;
        while(true) {
            switch(p->type()) {
            case ContextType::CANCEL:
                return std::dynamic_pointer_cast<CancelContext, Context_>(p);
            case ContextType::VALUE:
                p = ctx->_parent;
                break;
            default:
                return nullptr;
            }
        }
    }

private:
    //Context _parent;
    std::vector<std::shared_ptr<CancelContext>> _children;
    Chan<int> _done;
    std::thread _th;
    std::mutex _mu;
};

Context background() {
	return std::make_shared<EmptyContext>(nullptr);
}

Context with_value(const Context& parent, const std::string& key, const std::string& value) {
	return std::make_shared<ValueContext>(parent, key, value);
}

std::tuple<Context, std::function<void()>> with_cancel(const Context& parent) {
    auto p = std::make_shared<CancelContext>(parent);
    p->init();
    return std::make_tuple<Context, std::function<void()>>(p, [p](){
        p->cancel();
    });
}
