#include	<ctype.h>
#include	<float.h>
#include	<math.h>
#include	<stdlib.h>
#include	<stdint.h>

/*
 * $Header$
 */

#define	NDIG	FLT_DIG+3 	/* Number of digits (ANSI 5.2.4.2.2) */

#if		defined(_MPC_) && !defined(__DSPICC__) 		/* If Microchip PIC */
// undefining this macro can lead to more accurate results, but at the cost of much
// increased code size.
#define	SMALLCODE
#endif


#ifndef	SMALLCODE
/*
 *	Tables of powers of 10
 */
static const float	_powers_[] =
{
	1e0,
	1e1,
	1e2,
	1e3,
	1e4,
	1e5,
	1e6,
	1e7,
	1e8,
	1e9,
	1e10,
#if	FLT_MAX_10_EXP >= 20
	1e20,
	1e30,
#endif
#if	FLT_MAX_10_EXP >= 40
	1e40,
	1e50,
	1e60,
	1e70,
	1e80,
	1e90,
#endif
#if	FLT_MAX_10_EXP >= 100
	1e100,
	1e200,
	1e300,
#endif
};

#if 0
static const float	_npowers_[] =
{
	1e-0,
	1e-1,
	1e-2,
	1e-3,
	1e-4,
	1e-5,
	1e-6,
	1e-7,
	1e-8,
	1e-9,
	1e-10,
#if	FLT_MIN_10_EXP <= -20
	1e-20,
	1e-30,
#endif
#if	FLT_MIN_10_EXP <= -40
	1e-40,
	1e-50,
	1e-60,
	1e-70,
	1e-80,
	1e-90,
#endif
#if	FLT_MIN_10_EXP <= -100
	1e-100,
	1e-200,
	1e-300,
#endif
};
#endif
#endif

#define CSTRLEN(s)	((sizeof (s) / sizeof (s)[0])-1)

/*
 *	String to float
 */
float
strtof(const char * __restrict s, char ** __restrict res)
{
	char	flags;
    int i;

/*	values in flags :
 */

#define	ISNEG	1	/* Number is negative */
#define	DIGSEEN	2	/* We saw at least one digit */
#define	EXPNEG	8	/* Exponent is negative */
#define	DOTSEEN	16	/* We have seen a dot */

	unsigned char digit;
	signed int	expon;
	signed int	eexp;
	union {			/* A union to hold the integer	*/
		float	_l;	/* component and then the resultant */
		uint32_t	_v;
	}	_u;
#define	v	_u._v
#define	l	_u._l

	if(res)
		*res = (char*) s;
	while(isspace(eexp = *s))
		s++;
	flags = 0;			/* Reset flags */
	if(eexp == '-') {	/* Check for sign */
		flags = ISNEG;
		s++;
	} else if(eexp == '+')
		s++;

	// check for infinity
	for (i = 0; s[i] != '\0' && (s[i] == "infinity"[i] || s[i] == "INFINITY"[i]); i++) {
		continue;
	}
	if (i >= CSTRLEN("inf")) {
		l = INFINITY;
		if (res != NULL) {
			if (i != CSTRLEN("infinity")) {
				i = CSTRLEN("inf");
			}
			*res = (char*)s+i;
		}
		goto exit_strtof;
	}
	// check for NaN
	for (i = 0; s[i] != '\0' && (s[i] == "nan"[i] || s[i] == "NAN"[i]); i++) {
		continue;
	}
	if (i == CSTRLEN("nan")) {
		l = NAN;
		if (res != NULL) {
			s += i;
			if (*s == '(') {
				for (i = 0; s[i] != '\0' && s[i] != ')'; i++) {
					continue;
				}
				if (s[i] == ')') {
					s += i + 1;
				}
			}
			*res = (char*)s;
		}
		goto exit_strtof;
	}

	eexp = 0;			/* Clear digit count */
	v = 0;				/* Clear integer component */
	expon = 0;			/* Total exponent for integer */

	// 6.4.4.2 hexadecimal floating constants
	for (i = 0; s[i] == '0'; i++) {
		// find first non-zero character
		continue;
	}
	if (i != 0 && (s[i] == 'x' || s[i] == 'X')) {
		s += i+1;
		// read the hexadecimal significand.
		// skip leading zeros
		for (i = 0; s[i] == '0'; i++) {
			continue;
		}
		// first the integer part
		for (i = 0; 1; i++) {
			if (s[i] == 'p' || s[i] == 'P') {
				break;
			}
			else if (s[i] == '.') {
				flags |= DOTSEEN;
				i++;
				break;
			}
			else if (isxdigit(s[i])) {
				digit = 0;
				if (isdigit(s[i])) {
					digit = s[i] - '0';
				}
				else {
					digit = toupper(s[i]) - 'A' + 10;
				}
				if (eexp < 8) {
					v |= ((uint32_t)digit << ((8 - eexp - 1) * 4));
				}
				eexp++;
				expon += 4;
			}
			else {
				return 0.0f;
			}

		}
		// fraction part
		for (; flags & DOTSEEN ;i++) {
			if (s[i] == 'p' || s[i] == 'P') {
				break;
			}
			else if (s[i] == '0' && eexp == 0) {
				expon -= 4;
			}
			else if (isxdigit(s[i])) {
				digit = 0;
				if (isdigit(s[i])) {
					digit = s[i] - '0';
				}
				else {
					digit = toupper(s[i]) - 'A' + 10;
				}
				if (eexp < 8) {
					v |= ((int32_t)digit << ((8 - eexp - 1) * 4));
				}
				eexp++;
			}
		}
		// exponent
		if (s[i] != 'p' && s[i] != 'P') {
			return 0.0f;
		}
		i++;
		if (s[i] == '+') {
		   // skip the '+'
		   i++;
		}
		else if (s[i] == '-') {
			flags |= EXPNEG;
		   i++;
		}
		s += i;
		for (i = 0; s[i] == '0'; i++) {
			continue; // skip leading zeros
		}
		eexp = 0;
		for (; isdigit(s[i]); i++) {
			if (i != 0) {
				eexp *= 10;
			}
			eexp += s[i] - '0';
		}
		if (i == 0) {
			return 0.0f;
		}
		if (flags & EXPNEG) {
			eexp = -eexp;
		}
		expon += eexp;
		if (res != NULL) {
			*res = (char*)s+i;
		}
		if (v == 0) {
			return 0.0f;
		}
		// round-up mantissa
		if ((v & 0xFF) > 0x80) {
			v += 0x100;
		}
		while (!(v & INT32_C(0x80000000))) {
				v <<= 1;
				expon--;
		}
		// handle denorms
#ifdef __XC8__
		if (expon <= -126) {
			expon = -127;
			v = 0;
		}
#else
		if (expon <= -126) {
			while (expon <= -127 && v != 0 ) {
				expon++;
				v >>= 1;
			}
			expon = -127;
		}
#endif
		// normalize
		if (expon != -127) {
			v <<= 1;
			expon--;
		}
		// got an inifinity?
		if (expon > 127) {
			v = 0;
			expon = 128;
		}
		// assemble float
		v >>= 9;
		v &= INT32_C(0x7FFFFF);
		v |= (int32_t)((expon+127) & 0xFF) << 23;
		goto exit_strtof;
	}

	for(;;) {
		if(!(flags & DOTSEEN) && *s == '.') {
			flags |= DOTSEEN;		/* If into decimal set flag */
			s++;
			continue;
		}
		if(!isdigit(*s))			/* If end of valid sequence end */
			break;
		flags |= DIGSEEN;			/* else set flag */
		if(eexp != NDIG) {
			if(flags & DOTSEEN)		/* Count decimal  places */
				expon--;
			eexp++;
			v *= 10;
			v += (unsigned char)(*s - '0');	/* Convert digits to integer part */
		} else if(!(flags & DOTSEEN))
			expon++;
		s++;
	}

	eexp = 0;						/* Zero users exponent */
	if(*s == 'e' || *s == 'E') {	/* Look at exponent */
		if(*++s == '-') {			/* Check sign */
			flags |= EXPNEG;
			s++;
		} else if(*s == '+')
			s++;
		while(*s=='0')s++;		/* Skip leading zeros */
		if(isdigit(*s)) {		/* Read three digits */
			eexp = (unsigned char)(*s++ - '0');
			if(isdigit(*s)) {
				eexp = eexp*10 + (unsigned char)(*s++ - '0');
				if(isdigit(*s))
					eexp = eexp*10 + (unsigned char)(*s - '0');
			}
		}
		if(flags & EXPNEG)
			eexp = -eexp;
	}
	expon += eexp;			/* Add user and integer exponents */

	if(res && flags & DIGSEEN)
		*res = (char*) s;
	l = (float)v;			/* Convert the integer component to a float */
	if(l == 0.0)			/* simply return zero */
		return 0.0;
	if(expon < 0) {
		expon = -expon;
#if	FLT_MAX_10_EXP >= 100
		while(expon >= 100) {
			l /= 1e+100;
			expon -= 100;
		}
#endif
#ifdef	SMALLCODE
		while(expon >= 10) {
			l /= 1e+10;
			expon -= 10;
		}
		while(expon != 0) {
			l /= 1e+1;
			expon--;
		}
#else
		if(expon >= 10) {
			l /= _powers_[expon/10+9];
			expon %= 10;
		}
		if(expon)
			l /= _powers_[expon];
#endif	/* SMALLCODE */
		if(l < FLT_MIN) {
			if(flags & ISNEG)
				return -FLT_MIN;
			return FLT_MIN;
		}
	} else if(expon > 0) {
#if	FLT_MAX_10_EXP >= 100
		while(expon >= 100) {
			l *= 1e100;
			expon -= 100;
		}
#endif
#ifdef	SMALLCODE
		while((unsigned int)expon >= 10) {
			l *= 1e10;
			expon -= 10;
		}
		while(expon != 0) {
			l *= 1e1;
			expon--;
		}
#else
		if(expon >= 10) {
			l *= _powers_[expon/10+9];
			expon %= 10;
		}
		if(expon)
			l *= _powers_[expon];
#endif	/* SMALLCODE */
		if(l > FLT_MAX) {
			if(flags & ISNEG)
				return -FLT_MIN;
			return FLT_MIN;
		}
	}
exit_strtof:
	if(flags & ISNEG)
		l = -l;
	return l;
}
