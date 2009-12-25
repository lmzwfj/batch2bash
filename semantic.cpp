#include "semantic.h"
#include <stdexcept>
#include <algorithm>
#include <stack>
#include <set>

enum block_type { bSIMPLE, bCOND, bLABEL, bHARD };

struct block {
    std::vector<block*> child_blocks;
    std::vector<jump> child_jumps;
    command comm;
    block_type type;
    block(const command& comm) : comm(comm) {}
    union jump_label {
        label *labl;
        jump *jmp;
    } jmplabel;
    friend block operator +(const block& lhs, const block& rhs);
};

block operator +(const block& lhs, const block& rhs) {
    block ret(lhs);
    ret.child_blocks.insert(ret.child_blocks.end(), rhs.child_blocks.begin(),
            rhs.child_blocks.end());
    ret.type = bSIMPLE; // right?
    return ret;
}

enum s_type { NONE = -1, LABEL, JUMP };
struct previous {
    s_type type;
    std::vector<block*>::iterator pos;
    previous(s_type type, std::vector<block*>::iterator pos) : type(type), pos(pos) {}
};
block* translate(command* parent, program* shared_program) {
    block* ret = new block(*parent);
    if (parent->children.empty()) {
        if (parent->type == cJUMP) { 
            ret->type = bCOND;
            ret->jmplabel.jmp = &shared_program->jumps.get_jump(parent->line);
        } else if (parent->type == cLABEL) {
            ret->type = bLABEL;
            ret->jmplabel.labl = &shared_program->labels.get_label(parent->name);
        } else {
            ret->type = bSIMPLE;
        }
        return ret;
    }
    std::vector<block*> child_blocks;
    std::vector<label> labels;
    std::vector<std::vector<jump> > child_jumps; //recursively
    block* result;
    for (std::vector<command*>::iterator i = parent->children.begin(); 
            i != parent->children.end(); ++i) {
        result = translate(*i, shared_program);
        switch (result->type) {
            case bSIMPLE :
                if (!child_blocks.empty() && child_blocks.back()->type == bSIMPLE) {
                    *(child_blocks.back()) = *(child_blocks.back()) + *result;
                    // simple_block : simple_block simple_block;
                } else {
                    child_blocks.push_back(result);
                }
                break;
            case bCOND : 
                child_blocks.push_back(result);
                child_jumps.push_back(result->child_jumps);
                break;
            case bHARD :
                // scream recursively ;)
                break;
            case bLABEL : // just one label, in no block
                child_blocks.push_back(result);
                labels.push_back(*(result->jmplabel.labl));
                break;
        }
    }

    // first find all hidden loops and convert them to simple blocks
    int label_counter = 0, jump_counter = 0;
    int line = -1;
    std::stack<previous> prev;
    for (std::vector<block*>::iterator i = child_blocks.begin(); i != child_blocks.end();
            ++i) {
        line = (*i)->comm.line;
        if (label_counter < labels.size() && line == labels[label_counter].line) {
            if (labels[label_counter].jumps.size() == 1 && prev.top().type == JUMP &&
                    child_jumps[jump_counter - 1].size() == 1 &&
                    labels[label_counter].jumps.back() == child_jumps[jump_counter - 1].front()) {
                // COND1 SIMPLE* LABEL1 -> COND1 IF!1 SIMPLE* FI!1
                //block new_block = generate_hidden_if(prev.top().pos, i);
                child_blocks.erase(prev.top().pos + 1, i++);
//                *(prev.top().pos) = new_block;
                labels.erase(labels.begin() + label_counter);
                child_jumps.erase(child_jumps.begin() + jump_counter - 1);
                prev.pop();
            } else {
                prev.push(previous(LABEL, i));
                ++label_counter;
            }
		} else if (jump_counter < child_jumps.size() && line == child_jumps[jump_counter].front().line) {
            if (child_jumps[jump_counter].size() == 1 && prev.top().type == LABEL &&
                    labels[label_counter - 1].jumps.size() == 1 && 
                    labels[label_counter - 1].jumps.back() == child_jumps[jump_counter].front()) {
                // LABEL1 SIMPLE* COND1 -> WHILE1 SIMPLE* COND1 DONE
     //           block new_block = generate_hidden_while(prev.top().pos, i);
                child_blocks.erase(prev.top().pos + 1, ++i);
     //           *(prev.top().pos) = new_block;
                labels.erase(labels.begin() + label_counter - 1);
                child_jumps.erase(child_jumps.begin() + jump_counter);
                prev.pop();
            } else {
                prev.push(previous(JUMP, i));
                ++jump_counter;
            }
        }
    }
/* 
 *  LEAVING MULTIJUMPS FOR LATER... first test this
    // now if there are any remaining labels and corresponding jumps enclose it all in a big while loop and put ifs inside
    for (int i = 0; i < child_blocks.size(); ++i) {

    }
*/
    ret->child_blocks = child_blocks;
	for (std::vector<std::vector<jump> >::iterator i = child_jumps.begin(); i != child_jumps.end(); ++i) {
		for (std::vector<jump>::iterator j = i->begin(); j != i->end(); ++j) {
			ret->child_jumps.push_back(*j);
		}
	}
    if (labels.size() > 0) { // there are some labels in this block to which no jump has been performed to
        ret->type = bHARD;
    } else if (child_jumps.size() > 0) {
        ret->type = bCOND;
    } else {
        ret->type = bSIMPLE;
    }
    return ret;        
}

void program::done() { 
	for (std::vector<jump>::iterator i = jumps.jumps.begin(); i != jumps.jumps.end(); ++i) {
		labels.add_jump(*i);
    } 
    label_list unused = labels;
    // read the info file, all the documentation is there
	for (std::vector<jump>::iterator i = jumps.jumps.begin(); i != jumps.jumps.end(); ++i) {
		if (unused.label_exists(i->label)) {
			unused.remove_label(i->label);
        } else {
            throw std::logic_error("jump to an unknown label");
        }
    }
	for (std::vector<label>::iterator i = unused.labels.begin(); i != unused.labels.end(); ++i) {
        labels.remove_label(i->name);
    }
}

void program::generate_bash() {
    block* ret = translate(root, this);
}

bool label_list::label_exists(const std::string& name) const {
    for (std::vector<label>::const_iterator i = labels.begin(); i != labels.end(); ++i) {
        if (i->name == name) {
            return true;
        }
    }
    return false;
}

void label_list::add_label(const std::string& name, int line) {
    for (std::vector<label>::iterator i = labels.begin(); i != labels.end(); ++i) {
        if (i->name == name) {
            throw std::logic_error("label already exists"); 
        }
    }
    labels.push_back(label(name, line));
}
void label_list::remove_label(const std::string& name) {
    labels.erase(std::find(labels.begin(), labels.end(), label(name, 42)));
}
unsigned label_list::num_labels() const {
    return labels.size();
}

void label_list::add_jump(const jump& jmp) {
    std::vector<label>::iterator lab = find(labels.begin(), labels.end(), label(jmp.label, 42));
	if (lab == labels.end()) {
        throw std::logic_error("label doesn't exists"); 
    } 
    if (find(lab->jumps.begin(), lab->jumps.end(), jmp) == lab->jumps.end()) {
        lab->jumps.push_back(jmp);
    }
}

const label& label_list::get_label(std::string& name) const {
    return *(find(labels.begin(), labels.end(), label(name, 42)));
}

label& label_list::get_label(std::string& name) {
    return *(find(labels.begin(), labels.end(), label(name, 42)));
}


const jump& jump_list::get_jump(unsigned line) const {
    return *(find(jumps.begin(), jumps.end(), jump(std::string("42"), line)));
}

jump& jump_list::get_jump(unsigned line) {
    return *(find(jumps.begin(), jumps.end(), jump(std::string("42"), line)));
}

int operator <(const jump& lhs, const jump& rhs) {
	return lhs.line < rhs.line;
}

int operator ==(const jump& lhs, const jump& rhs) {
	return lhs.line == rhs.line;
}

int operator <(const label& lhs, const label& rhs) {
	return lhs.name < rhs.name;
}

int operator ==(const label& lhs, const label& rhs) {
	return lhs.name == rhs.name;
}

void command::add_child(command* child) {
    children.push_back(child);
}

program::program() { 
    root = NULL;
}

struct tree_frame {
    command* com;
    int visited;
    tree_frame(command* com, int visited = 0) : com(com), visited(visited) { }
};

void program::print_program_tree() const {
    std::stack<tree_frame> parents;
    parents.push(tree_frame(root));
    tree_frame* current;
    int level = 0;
    printf("\nsemantic command tree:\n");
    while (!parents.empty()) {
        current = &parents.top();
        if (!current->visited) {
            for (int i = 0; i < level; ++i) {
                printf("\t");
            }
            printf("%s%d\n", current->com->name.c_str(), current->com->children.size());
        }
        if (current->visited < current->com->children.size()) {
            parents.push(tree_frame(current->com->children[current->visited++]));
            ++level;
        } else {
            parents.pop();
            --level;
        }
    }
}