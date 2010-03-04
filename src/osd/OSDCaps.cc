
#include "OSDCaps.h"

#include "config.h"


bool OSDCaps::get_next_token(string s, size_t& pos, string& token)
{
  int start = s.find_first_not_of(" \t", pos);
  int end;

  if (s[start] == '=' || s[start] == ',' || s[start] == ';') {
    end = start + 1;
  } else {
    end = s.find_first_of(";,= \t", start+1);
  }

  if (start < 0) {
    return false; 
  }

  if (end < 0) {
    end=s.size();
  }

  token = s.substr(start, end - start);

  pos = end;

  return true;
}

bool OSDCaps::is_rwx(string& token, rwx_t& cap_val)
{
  const char *t = token.c_str();
  int val = 0;

  while (*t) {
    switch (*t) {
    case 'r':
      val |= OSD_POOL_CAP_R;
      break;
    case 'w':
      val |= OSD_POOL_CAP_W;
      break;
    case 'x':
      val |= OSD_POOL_CAP_X;
      break;
    default:
      return false;
    }
    t++;
  }

  cap_val = val;
  return true;
}

bool OSDCaps::parse(bufferlist::iterator& iter)
{
  string s;

  try {
    ::decode(s, iter);

    generic_dout(0) << "decoded caps: " << s << dendl;

    size_t pos = 0;
    string token;
    bool init = true;

    bool op_allow = false;
    bool op_deny = false;
    bool cmd_pool = false;
    bool any_cmd = false;
    bool got_eq = false;
    list<int> num_list;
    bool last_is_comma = false;
    rwx_t cap_val = 0;

    while (pos < s.size()) {
      if (init) {
        op_allow = false;
        op_deny = false;
        cmd_pool = false;
        any_cmd = false;
        got_eq = false;
        last_is_comma = false;
        cap_val = 0;
        init = false;
        num_list.clear();
      }

#define ASSERT_STATE(x) \
do { \
  if (!(x)) { \
       *_dout << "error parsing caps at pos=" << pos << " (" #x ")" << std::endl; \
  } \
} while (0)

      if (get_next_token(s, pos, token)) {
	if (token.compare("auth_uid") == 0) {
	  get_next_token(s, pos, token);
	  auth_uid = strtol(token.c_str(), NULL, 10);
        } else if (token.compare("=") == 0) {
          ASSERT_STATE(any_cmd);
          got_eq = true;
        } else if (token.compare("allow") == 0) {
          ASSERT_STATE((!op_allow) && (!op_deny));
          op_allow = true;
        } else if (token.compare("deny") == 0) {
          ASSERT_STATE((!op_allow) && (!op_deny));
          op_deny = true;
        } else if ((token.compare("pools") == 0) ||
                   (token.compare("pool") == 0)) {
          ASSERT_STATE(op_allow || op_deny);
          cmd_pool = true;
          any_cmd = true;
        } else if (is_rwx(token, cap_val)) {
          ASSERT_STATE(op_allow || op_deny);
        } else if (token.compare(";") != 0) {
          ASSERT_STATE(got_eq);
          if (token.compare(",") == 0) {
            ASSERT_STATE(!last_is_comma);
          } else {
            last_is_comma = false;
            int num = strtol(token.c_str(), NULL, 10);
            num_list.push_back(num);
          }
        }

        if (token.compare(";") == 0 || pos >= s.size()) {
          if (got_eq) {
            ASSERT_STATE(num_list.size() > 0);
            list<int>::iterator iter;
            for (iter = num_list.begin(); iter != num_list.end(); ++iter) {
              OSDPoolCap& cap = pools_map[*iter];
              if (op_allow) {
                cap.allow |= cap_val;
              } else {
                cap.deny |= cap_val;
              }
            }
          } else {
            if (op_allow) {
              default_action |= cap_val;
            } else {
              default_action &= ~cap_val;
            }
          }
          init = true;
        }
        
      }
    }
  } catch (buffer::error *err) {
    return false;
  }

  generic_dout(0) << "default=" << (int)default_action << dendl;
  map<int, OSDPoolCap>::iterator it;
  for (it = pools_map.begin(); it != pools_map.end(); ++it) {
    generic_dout(0) << it->first << " -> (" << (int)it->second.allow << "." << (int)it->second.deny << ")" << dendl;
  }

  return true;
}

int OSDCaps::get_pool_cap(int pool_id, __u64 uid)
{
  int cap = default_action;

  if (allow_all)
    return OSD_POOL_CAP_ALL;

  map<int, OSDPoolCap>::iterator iter = pools_map.find(pool_id);
  if (iter != pools_map.end()) {
    OSDPoolCap& c = iter->second;
    cap |= c.allow;
    cap &= ~c.deny;
  } else if (	uid != CEPH_AUTH_UID_DEFAULT
	     && uid == auth_uid) {
    //the owner has full access unless they've removed some by setting
    //new caps
    cap = OSD_POOL_CAP_ALL;
  }

  return cap;
}

