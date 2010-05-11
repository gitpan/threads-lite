#define PERL_NO_GET_CONTEXT
#include "EXTERN.h"
#include "perl.h"
#define NO_XSLOCKS
#include "XSUB.h"

#include "message.h"

/*
 * struct message
 */

static SV* S_message_get_sv(pTHX_ message* message) {
	SV* stored = newSV_type(SVt_PV);
	SvPVX(stored) = message->string.ptr;
	SvLEN(stored) = SvCUR(stored) = message->string.length;
	SvPOK_only(stored);
	return stored;
}

#define message_get_sv(message) S_message_get_sv(aTHX_ message)

static void S_message_set_sv(pTHX_ message* message, SV* value, enum message_type type) {
	message->type = type;
	char* string = SvPV(value, message->string.length);
	message->string.ptr = savepvn(string, message->string.length);
}

#define message_set_sv(message, value, type) S_message_set_sv(aTHX_ message, value, type)

void S_message_store_value(pTHX_ message* message, SV* value) {
	dSP;
	ENTER;
	SAVETMPS;
	sv_setiv(save_scalar(gv_fetchpv("Storable::Deparse", TRUE | GV_ADDMULTI, SVt_PV)), 1);
	PUSHMARK(SP);
	XPUSHs(sv_2mortal(newRV_inc(value)));
	PUTBACK;
	call_pv("Storable::mstore", G_SCALAR);
	SPAGAIN;
	message_set_sv(message, POPs, STORABLE);
	FREETMPS;
	LEAVE;
	PUTBACK;
}

static inline int S_is_simple(pTHX_ SV* value) {
	return SvOK(value) && !SvROK(value) && !(SvPOK(value) && SvUTF8(value));
}
#define is_simple(value) S_is_simple(aTHX_ value)

static inline int S_are_simple(pTHX_ SV** begin, SV** end) {
	SV** current;
	for(current = begin; current <= end; current++)
		if (! is_simple(*current))
			return FALSE;
	return TRUE;
}

#define are_simple(begin, end) S_are_simple(aTHX_ begin, end)

static const char pack_template[] = "(I/a)*";

void S_message_from_stack(pTHX_ message* message) {
	dSP; dMARK;
	if (SP == MARK && is_simple(*SP)) {
		message_set_sv(message, MARK[0], STRING);
	}
	else if (are_simple(MARK + 1, SP)) {
		SV* tmp = sv_2mortal(newSVpvn("", 0));
		packlist(tmp, pack_template, pack_template + sizeof pack_template - 1, MARK + 1, SP + 1);
		message_set_sv(message, tmp, PACKED);
	}
	else {
		SV* list = sv_2mortal((SV*)av_make(SP - MARK, MARK + 1));
		message_store_value(message, list);
	}
}

SV* S_message_load_value(pTHX_ message* message) {
	dSP;

	sv_setiv(save_scalar(gv_fetchpv("Storable::Eval", TRUE | GV_ADDMULTI, SVt_PV)), 1);
	PUSHMARK(SP);
	XPUSHs(sv_2mortal(message_get_sv(message)));
	PUTBACK;
	call_pv("Storable::thaw", G_SCALAR);
	SPAGAIN;
	SV* ret = POPs;
	PUTBACK;
	return ret;
}

void S_message_to_stack(pTHX_ message* message, U32 context) {
	dSP;
	switch(message->type) {
		case STRING:
			PUSHs(sv_2mortal(newRV_noinc(message_get_sv(message))));
			break;
		case PACKED: {
			SV* mess = message_get_sv(message);
			STRLEN len;
			const char* packed = SvPV(mess, len);
			PUTBACK;
			unpackstring(pack_template, pack_template + sizeof pack_template - 1, packed, packed + len, 0);
			SPAGAIN;
			break;
		}
		case STORABLE: {
			AV* values = (AV*) SvRV(message_load_value(message));
			SPAGAIN;

			if (context == G_SCALAR) {
				SV** ret = av_fetch(values, 0, FALSE);
				PUSHs(ret ? *ret : &PL_sv_undef);
			}
			else if (context == G_ARRAY) {
				UV count = av_len(values) + 1;
				EXTEND(SP, count);
				Copy(AvARRAY(values), SP + 1, count, SV*);
				SP += count;
			}
			break;
		}
		default:
			Perl_croak(aTHX_ "Type %d is not yet implemented", message->type);
	}

	PUTBACK;
}

void S_message_to_array(pTHX_ message* message, AV** array_ptr) {
	dSP;
	switch(message->type) {
		case STRING:
			*array_ptr = newAV();
			av_push(*array_ptr, message_get_sv(message));
			sv_2mortal((SV*)*array_ptr);
			break;
		case PACKED: {
			SV* mess = message_get_sv(message);
			STRLEN len;
			const char* packed = SvPV(mess, len);
			SV** mark = SP;
			PUTBACK;
			int count = unpackstring(pack_template, pack_template + sizeof pack_template - 1, packed, packed + len, 0);
			SPAGAIN;
			*array_ptr = av_make(count, mark + 1);
			sv_2mortal((SV*)*array_ptr);
			break;
		}
		case STORABLE: {
			*array_ptr = (AV*) SvRV(message_load_value(message));
			SPAGAIN;
			break;
		}
		default:
			Perl_croak(aTHX_ "Type %d is not yet implemented", message->type);
	}

	PUTBACK;
}

void S_message_clone(pTHX_ message* origin, message* clone) {
	clone->type = origin->type;
	switch (origin->type) {
		case EMPTY:
			break;
		case STRING:
		case PACKED:
		case STORABLE:
			clone->string.length = origin->string.length;
			clone->string.ptr = savepvn(origin->string.ptr, origin->string.length);
			break;
		default:
			Perl_die(aTHX, "Unknown type in message\n");
	}
}

void message_destroy(message* message) {
	switch (message->type) {
		case EMPTY:
			break;
		case STRING:
		case STORABLE:
			Safefree(message->string.ptr);
			Zero(message, 1, message);
			break;
/*		default:
			Perl_die(aTHX, "Unknown type in message\n"); */
	}
}
