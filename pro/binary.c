#include "jesspro.h"

#include "jessfs.h"

void pro_binary_read(JRP *jrp)
{
	int offset, size;
	int retval;
	struct list_head *pos, *n;
	JRP *optjrp;
	struct jesspro_task *task;
	unsigned char *uptr;

	offset = 0;
	size = -1;
	task = (struct jesspro_task *)jrp->taskptr;

	if (task->size_jrp_option <= 0) {
		pro_append_response(task, JESS_FATAL);
		return;
	}

	/* determine the size and offset specified in option */
	pos = NULL;
	n = NULL;
	list_for_each_safe(pos, n, &task->head_jrp_option) {
		optjrp = containing_record(pos, JRP, entry);
		if (1 == optjrp->depth) {
			if (0 == strcmp("size", optjrp->segm[0].name)) {
				size = atoi(optjrp->dupvalue);
			}

			if (0 == strcmp("offset", optjrp->segm[0].name)) {
				offset = atoi(optjrp->dupvalue);
			}
		}
	}

	if (size < 0) {
		pro_append_response(task, JESS_FATAL);
		return;
	}

	retval = fs_read_binary(offset, size, &uptr);
	if (retval != size) {
		pro_append_response(task, JESS_FATAL);
		return;
	}

	pro_append_response(task, JESS_OK);
	pro_append_binary_response(task, uptr, size);
}

void pro_binary_write(JRP *jrp)
{
	int offset, size;
	int retval;
	struct list_head *pos, *n;
	JRP *optjrp;
	struct jesspro_task *task;

	offset = 0;
	size = -1;
	task = (struct jesspro_task *)jrp->taskptr;

	if (task->size_jrp_option <= 0) {
		pro_append_response(jrp->taskptr, JESS_FATAL);
		return;
	}

	pos = NULL;
	n = NULL;
	/* determine the size and offset specified in option */
	list_for_each_safe(pos, n, &task->head_jrp_option) {
		optjrp = containing_record(pos, JRP, entry);
		if (1 == optjrp->depth) {
			if (0 == strcmp("size", optjrp->segm[0].name)) {
				size = atoi(optjrp->dupvalue);
			}

			if (0 == strcmp("offset", optjrp->segm[0].name)) {
				offset = atoi(optjrp->dupvalue);
			}
		}
	}

	if (size < 0 || size != jrp->valuestring.len) {
		pro_append_response(task, JESS_FATAL);
		return;
	}

	retval = fs_write_binary(offset, size, (const unsigned char *)jrp->valuestring.cstrptr);
	pro_append_response(task, (retval < 0) ? JESS_FATAL : JESS_OK);
}
