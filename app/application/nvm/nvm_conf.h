#ifndef NVM_CONF_H_
#define NVM_CONF_H_

// Default capacity for application-owned registry arrays.
// Each Manager binds its actual slot count; this is not a framework limit.
// Edit the default here or override it with a project-wide compiler define.
#ifndef NVM_REGISTER_MAX
#define NVM_REGISTER_MAX 16U
#endif

#endif /* NVM_CONF_H_ */
