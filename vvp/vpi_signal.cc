/*
 * Copyright (c) 2001-2006 Stephen Williams (steve@icarus.com)
 *
 *    This source code is free software; you can redistribute it
 *    and/or modify it in source code form under the terms of the GNU
 *    General Public License as published by the Free Software
 *    Foundation; either version 2 of the License, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */
#ifdef HAVE_CVS_IDENT
#ident "$Id: vpi_signal.cc,v 1.74 2006/02/21 05:31:54 steve Exp $"
#endif

/*
 * vpiReg handles are handled here. These objects represent vectors of
 * .var objects that can be manipulated by the VPI module.
 */

# include  "vpi_priv.h"
# include  "schedule.h"
# include  "statistics.h"
# include  <math.h>
# include  <iostream>
# include  <stdio.h>
#ifdef HAVE_MALLOC_H
# include  <malloc.h>
#endif
# include  <stdlib.h>
# include  <string.h>
# include  <assert.h>

/*
 * Hex digits that represent 4-value bits of Verilog are not as
 * trivially obvious to display as if the bits were the usual 2-value
 * bits. So, although it is possible to write a function that
 * generates a correct character for 4*4-value bits, it is easier to
 * just perform the lookup in a table. This only takes 256 bytes,
 * which is not many executable instructions:-)
 *
 * The table is calculated as compile time, therefore, by the
 * draw_tt.c program.
 */
extern const char hex_digits[256];
extern const char oct_digits[256];

/*
 * The string values need a result buf to hold the results. This
 * buffer can be reused for that purpose. Whenever I have a need, the
 * need_result_buf function makes sure that need can be met.
 */
char *need_result_buf(unsigned cnt, vpi_rbuf_t type)
{
      cnt = (cnt + 0x0fff) & ~0x0fff;

      static char*result_buf[2] = {0, 0};
      static size_t result_buf_size[2] = {0, 0};

      if (result_buf_size[type] == 0) {
	    result_buf[type] = (char*)malloc(cnt);
	    result_buf_size[type] = cnt;
      } else if (result_buf_size[type] < cnt) {
	    result_buf[type] = (char*)realloc(result_buf[type], cnt);
	    result_buf_size[type] = cnt;
      }

      return result_buf[type];
}

/*
 * implement vpi_get for vpiReg objects.
 */
static int signal_get(int code, vpiHandle ref)
{
      assert((ref->vpi_type->type_code==vpiNet)
	     || (ref->vpi_type->type_code==vpiReg));

      struct __vpiSignal*rfp = (struct __vpiSignal*)ref;

      switch (code) {

	  case vpiSigned:
	    return rfp->signed_flag != 0;

	  case vpiSize:
	    if (rfp->msb >= rfp->lsb)
		  return rfp->msb - rfp->lsb + 1;
	    else
		  return rfp->lsb - rfp->msb + 1;

	  case vpiNetType:
	    if (ref->vpi_type->type_code==vpiNet)
		  return vpiWire;
	    else
		  return 0;

	  case vpiLeftRange: return rfp->msb;
	  case vpiRightRange: return rfp->lsb;

	  case _vpiNexusId:
	    if (rfp->msb == rfp->lsb)
		  return (int) (unsigned long) rfp->node;
	    else
		  return 0;

	  default:
	    vpi_printf("signal_get: property %d is unknown\n", code);
	    return 0;
      }
}

static char* signal_get_str(int code, vpiHandle ref)
{
      assert((ref->vpi_type->type_code==vpiNet)
	     || (ref->vpi_type->type_code==vpiReg));

      struct __vpiSignal*rfp = (struct __vpiSignal*)ref;

      char *bn = strdup(vpi_get_str(vpiFullName, &rfp->scope->base));
      char *nm = (char*)rfp->name;

      char *rbuf = need_result_buf(strlen(bn) + strlen(nm) + 2, RBUF_STR);

      switch (code) {

	  case vpiFullName:
	    sprintf(rbuf, "%s.%s", bn, nm);
	    free(bn);
	    return rbuf;

	  case vpiName:
	    strcpy(rbuf, nm);
	    free(bn);
	    return rbuf;
      }

      free(bn);
      return 0;
}

static vpiHandle signal_get_handle(int code, vpiHandle ref)
{
      assert((ref->vpi_type->type_code==vpiNet)
	     || (ref->vpi_type->type_code==vpiReg));

      struct __vpiSignal*rfp = (struct __vpiSignal*)ref;

      switch (code) {

	  case vpiScope:
	    return &rfp->scope->base;

	  case vpiModule:
	      { struct __vpiScope*scope = rfp->scope;
	        while (scope && scope->base.vpi_type->type_code != vpiModule)
		      scope = scope->scope;

		assert(scope);
		return &scope->base;
	      }
      }

      return 0;
}


static char *signal_vpiDecStrVal(struct __vpiSignal*rfp, s_vpi_value*vp)
{
      unsigned wid = (rfp->msb >= rfp->lsb)
	    ? (rfp->msb - rfp->lsb + 1)
	    : (rfp->lsb - rfp->msb + 1);

      vvp_fun_signal_vec*vsig = dynamic_cast<vvp_fun_signal_vec*>(rfp->node->fun);
      assert(vsig);

	/* FIXME: bits should be an array of vvp_bit4_t. */
      unsigned char* bits = new unsigned char[wid];

      for (unsigned idx = 0 ;  idx < wid ;  idx += 1) {
	    bits[idx] = vsig->value(idx);
      }

      unsigned hwid = (wid+2) / 3 + 1;
      char *rbuf = need_result_buf(hwid, RBUF_VAL);

      vpip_bits_to_dec_str(bits, wid, rbuf, hwid, rfp->signed_flag);

      delete[]bits;

      return rbuf;
}


static char *signal_vpiStringVal(struct __vpiSignal*rfp, s_vpi_value*vp)
{
      unsigned wid = (rfp->msb >= rfp->lsb)
	    ? (rfp->msb - rfp->lsb + 1)
	    : (rfp->lsb - rfp->msb + 1);

      vvp_fun_signal*vsig = dynamic_cast<vvp_fun_signal*>(rfp->node->fun);

      /* The result will use a character for each 8 bits of the
	 vector. Add one extra character for the highest bits that
	 don't form an 8 bit group. */
      char *rbuf = need_result_buf(wid/8 + ((wid&7)!=0) + 1, RBUF_VAL);
      char *cp = rbuf;

      char tmp = 0;
      int bitnr;
      for(bitnr=wid-1; bitnr>=0; bitnr--){
	  tmp <<= 1;

	  switch (vsig->value(bitnr)) {
	  case BIT4_0:
	      break;
	  case  BIT4_1:
	      tmp |= 1;
	      break;
	  default:
	      break;
	  }

	  if ((bitnr&7)==0){
		  /* Skip leading nulls. */
		if (tmp == 0 && cp == rbuf)
		      continue;

		  /* Nulls in the middle get turned into spaces. */
		*cp++ = tmp? tmp : ' ';
		tmp = 0;
	  }
      }
      *cp++ = 0;

      return rbuf;
}

static unsigned signal_width(const struct __vpiSignal*rfp)
{
      unsigned wid = (rfp->msb >= rfp->lsb)
	    ? (rfp->msb - rfp->lsb + 1)
	    : (rfp->lsb - rfp->msb + 1);

      return wid;
}

static void signal_get_IntVal(struct __vpiSignal*rfp, s_vpi_value*vp)
{
      unsigned wid = signal_width(rfp);
      vvp_fun_signal_vec*vsig = dynamic_cast<vvp_fun_signal_vec*>(rfp->node->fun);

      assert(wid <= 8 * sizeof vp->value.integer);
      vp->value.integer = 0;

      for (unsigned idx = 0 ;  idx < wid ;  idx += 1) {
	    switch (vsig->value(idx)) {
		case BIT4_0:
		  break;
		case BIT4_1:
		  vp->value.integer |= 1<<idx;
		  break;
		default:
		    /* vpi_get_value of vpiIntVal treats x and z
		       values as 0. */
		  break;
	    }
      }
}

static void signal_get_ScalarVal(struct __vpiSignal*rfp, s_vpi_value*vp)
{
     vvp_fun_signal*vsig = dynamic_cast<vvp_fun_signal*>(rfp->node->fun);

      switch (vsig->value(0)) {
	  case BIT4_0:
	    vp->value.scalar = vpi0;
	    break;
	  case BIT4_1:
	    vp->value.scalar = vpi1;
	    break;
	  case BIT4_X:
	    vp->value.scalar = vpiX;
	    break;
	  case BIT4_Z:
	    vp->value.scalar = vpiZ;
	    break;
      }
}

static void signal_get_StrengthVal(struct __vpiSignal*rfp, s_vpi_value*vp)
{
     vvp_fun_signal_vec*vsig = dynamic_cast<vvp_fun_signal_vec*>(rfp->node->fun);
     unsigned wid = signal_width(rfp);
     s_vpi_strengthval*op;

     op = (s_vpi_strengthval*)
	   need_result_buf(wid * sizeof(s_vpi_strengthval), RBUF_VAL);

     for (unsigned idx = 0 ;  idx < wid ;  idx += 1) {
	   vvp_scalar_t val = vsig->scalar_value(idx);

	     /* vvp_scalar_t strengths are 0-7, but the vpi strength
		is bit0-bit7. This gets the vpi form of the strengths
		from the vvp_scalar_t strengths. */
	   unsigned s0 = 1 << val.strength0();
	   unsigned s1 = 1 << val.strength1();

	   switch (val.value()) {
	       case BIT4_0:
		 op[idx].logic = vpi0;
		 op[idx].s0 = s0|s1;
		 op[idx].s1 = 0;
		 break;
	       case BIT4_1:
		 op[idx].logic = vpi1;
		 op[idx].s0 = 0;
		 op[idx].s1 = s0|s1;
		 break;
	       case BIT4_X:
		 op[idx].logic = vpiX;
		 op[idx].s0 = s0;
		 op[idx].s1 = s1;
		 break;
	       case BIT4_Z:
		 op[idx].logic = vpiZ;
		 op[idx].s0 = vpiHiZ;
		 op[idx].s1 = vpiHiZ;
		 break;
	   }
     }

}

/*
 * The get_value method reads the values of the functors and returns
 * the vector to the caller. This causes no side-effect, and reads the
 * variables like a %load would.
 */
static void signal_get_value(vpiHandle ref, s_vpi_value*vp)
{
      assert((ref->vpi_type->type_code==vpiNet)
	     || (ref->vpi_type->type_code==vpiReg));

      struct __vpiSignal*rfp = (struct __vpiSignal*)ref;

      unsigned wid = signal_width(rfp);

      vvp_fun_signal_vec*vsig = dynamic_cast<vvp_fun_signal_vec*>(rfp->node->fun);
      assert(vsig);

      char *rbuf = 0;

      switch (vp->format) {

	  case vpiIntVal:
	    signal_get_IntVal(rfp, vp);
	    break;

	  case vpiScalarVal:
	    signal_get_ScalarVal(rfp, vp);
	    break;

	  case vpiStrengthVal:
	    signal_get_StrengthVal(rfp, vp);
	    break;

	  case vpiBinStrVal:
	    rbuf = need_result_buf(wid+1, RBUF_VAL);

	    for (unsigned idx = 0 ;  idx < wid ;  idx += 1) {
		  rbuf[wid-idx-1] = "01xz"[vsig->value(idx)];
	    }
	    rbuf[wid] = 0;
	    vp->value.str = rbuf;
	    break;

	  case vpiHexStrVal: {
		unsigned hwid = (wid + 3) / 4;

		rbuf = need_result_buf(hwid+1, RBUF_VAL);
		rbuf[hwid] = 0;

		vpip_vec4_to_hex_str(vsig->vec4_value(), rbuf, hwid+1, false);
		vp->value.str = rbuf;
		break;
	  }

	  case vpiOctStrVal: {
		unsigned hval, hwid;
		hwid = (wid + 2) / 3;

		rbuf = need_result_buf(hwid+1, RBUF_VAL);
		rbuf[hwid] = 0;
		hval = 0;
		for (unsigned idx = 0 ;  idx < wid ;  idx += 1) {
		      hval = hval | (vsig->value(idx) << 2*(idx % 3));

		      if (idx%3 == 2) {
			    hwid -= 1;
			    rbuf[hwid] = oct_digits[hval];
			    hval = 0;
		      }
		}

		if (hwid > 0) {
		      hwid -= 1;
		      rbuf[hwid] = oct_digits[hval];
		      unsigned padd = 0;
		      switch(rbuf[hwid]) {
			  case 'X': padd = 2; break;
			  case 'Z': padd = 3; break;
		      }
		      if (padd) {
			    for (unsigned idx = wid % 3; idx < 3; idx += 1) {
				  hval = hval | padd << 2*idx;
			    }
			    rbuf[hwid] = oct_digits[hval];
		      }
		}
		vp->value.str = rbuf;
		break;
	  }

	  case vpiDecStrVal:
	    vp->value.str = signal_vpiDecStrVal(rfp, vp);
	    break;

	  case vpiStringVal:
	    vp->value.str = signal_vpiStringVal(rfp, vp);
	    break;

	  case vpiVectorVal: {
	      unsigned int obit = 0;
	      unsigned hwid = (wid - 1)/32 + 1;

	      rbuf = need_result_buf(hwid * sizeof(s_vpi_vecval), RBUF_VAL);
	      s_vpi_vecval *op = (p_vpi_vecval)rbuf;
	      vp->value.vector = op;

	      op->aval = op->bval = 0;
	      for (unsigned idx = 0 ;  idx < wid ;  idx += 1) {
		switch (vsig->value(idx)) {
		case BIT4_0:
		  op->aval &= ~(1 << obit);
		  op->bval &= ~(1 << obit);
		  break;
		case BIT4_1:
		  op->aval |= (1 << obit);
		  op->bval &= ~(1 << obit);
		  break;
		case BIT4_X:
		  op->aval |= (1 << obit);
		  op->bval |= (1 << obit);
		  break;
		case BIT4_Z:
		  op->aval &= ~(1 << obit);
		  op->bval |= (1 << obit);
		  break;
		}
		obit++;
		if (!(obit % 32)) {
		      op += 1;
		      if ((op - vp->value.vector) < (ptrdiff_t)hwid)
			    op->aval = op->bval = 0;
		      obit = 0;
		}
	      }
	      break;
	    }

	  default:
	    fprintf(stderr, "vvp internal error: get_value: "
		    "value type %u not implemented."
		    " Signal is %s in scope %s\n",
		    vp->format, rfp->name, rfp->scope->name);
	    assert(0);
      }
}

/*
 * The put_value method writes the value into the vector, and returns
 * the affected ref. This operation works much like the %set or
 * %assign instructions and causes all the side-effects that the
 * equivalent instruction would cause.
 */

static vvp_vector4_t from_stringval(const char*str, unsigned wid)
{
      unsigned idx;
      const char*cp;

      cp = str + strlen(str);
      idx = 0;

      vvp_vector4_t val(wid, BIT4_0);

      while ((idx < wid) && (cp > str)) {
	    unsigned byte = *--cp;
	    int bit;

	    for (bit = 0 ;  bit < 8 ;  bit += 1) {
		  if (byte & 1)
			val.set_bit(idx, BIT4_1);

		  byte >>= 1;
		  idx += 1;
	    }
      }

      return val;
}

static vpiHandle signal_put_value(vpiHandle ref, s_vpi_value*vp)
{
      unsigned wid;
      struct __vpiSignal*rfp;

      assert((ref->vpi_type->type_code==vpiNet)
	     || (ref->vpi_type->type_code==vpiReg));

      rfp = (struct __vpiSignal*)ref;

	/* This is the destination that I'm going to poke into. Make
	   it from the vvp_net_t pointer, and assume a write to
	   port-0. This is the port where signals receive input. */
      vvp_net_ptr_t destination (rfp->node, 0);

	/* Make a vvp_vector4_t vector to receive the translated value
	   that we are going to poke. This will get populated
	   differently depending on the format. */
      wid = (rfp->msb >= rfp->lsb)
	    ? (rfp->msb - rfp->lsb + 1)
	    : (rfp->lsb - rfp->msb + 1);

      vvp_vector4_t val (wid, BIT4_0);

      switch (vp->format) {

	  case vpiIntVal: {
		long vpi_val = vp->value.integer;
		for (unsigned idx = 0 ;  idx < wid ;  idx += 1) {
		      vvp_bit4_t bit = vpi_val&1 ? BIT4_1 : BIT4_0;
		      val.set_bit(idx, bit);
		      vpi_val >>= 1;
		}
		break;
	  }

#if 0
	  case vpiScalarVal:
	    switch (vp->value.scalar) {
		case vpi0:
		  functor_poke(rfp, 0, 0, St0, 0);
		  break;
		case vpi1:
		  functor_poke(rfp, 0, 1, St1, 0);
		  break;
		case vpiX:
		  functor_poke(rfp, 0, 2, StX, 0);
		  break;
		case vpiZ:
		  functor_poke(rfp, 0, 3, HiZ, 0);
		  break;
		default:
		  assert(0);
	    }
	    break;
#endif
#if 0
	  case vpiVectorVal:
	    for (unsigned idx = 0 ;  idx < wid ;  idx += 1) {
		  unsigned long aval = vp->value.vector[idx/32].aval;
		  unsigned long bval = vp->value.vector[idx/32].bval;
		  aval >>= idx%32;
		  bval >>= idx%32;
		  int bit = (aval&1) | ((bval<<1)&2);
		  switch (bit) {
		      case 0: /* zero */
			functor_poke(rfp,idx, 0, St0, 0);
			break;
		      case 1: /* one */
			functor_poke(rfp,idx, 1, St1, 0);
			break;
		      case 2: /* z */
			functor_poke(rfp,idx, 3, HiZ, 0);
			break;
		      case 3: /* x */
			functor_poke(rfp,idx, 2, StX, 0);
			break;
		  }
	    }
	    break;
#endif
	  case vpiBinStrVal:
	    vpip_bin_str_to_vec4(val, vp->value.str, false);
	    break;
	  case vpiOctStrVal:
	    vpip_oct_str_to_vec4(val, vp->value.str);
	    break;
	  case vpiDecStrVal:
	    vpip_dec_str_to_vec4(val, vp->value.str, false);
	    break;
	  case vpiHexStrVal:
	    vpip_hex_str_to_vec4(val, vp->value.str);
	    break;
	  case vpiStringVal:
	    val = from_stringval(vp->value.str, wid);
	    break;

	  default:
	    fprintf(stderr, "vvp internal error: put_value: "
		    "value type %u not implemented."
		    " Signal is %s in scope %s\n",
		    vp->format, rfp->name, rfp->scope->name);
	    assert(0);

      }

      vvp_send_vec4(destination, val);

      return ref;
}

static const struct __vpirt vpip_reg_rt = {
      vpiReg,
      signal_get,
      signal_get_str,
      signal_get_value,
      signal_put_value,
      signal_get_handle,
      0
};

static const struct __vpirt vpip_net_rt = {
      vpiNet,
      signal_get,
      signal_get_str,
      signal_get_value,
      signal_put_value,
      signal_get_handle,
      0
};

/*
 * Construct a vpiIntegetVar object. Indicate the type using a flag
 * to minimize the code modifications. Icarus implements integers
 * as 'reg signed [31:0]'.
 */
vpiHandle vpip_make_int(const char*name, int msb, int lsb, vvp_net_t*vec)
{
      vpiHandle obj = vpip_make_net(name, msb,lsb, true, vec);
      struct __vpiSignal*rfp = (struct __vpiSignal*)obj;
      obj->vpi_type = &vpip_reg_rt;
      rfp->isint_ = true;
      return obj;
}

/*
 * Construct a vpiReg object. It's like a net, except for the type.
 */
vpiHandle vpip_make_reg(const char*name, int msb, int lsb,
			bool signed_flag, vvp_net_t*vec)
{
      vpiHandle obj = vpip_make_net(name, msb,lsb, signed_flag, vec);
      obj->vpi_type = &vpip_reg_rt;
      return obj;
}

static struct __vpiSignal* allocate_vpiSignal(void)
{
      static struct __vpiSignal*alloc_array = 0;
      static unsigned alloc_index = 0;
      const unsigned alloc_count = 512;

      if ((alloc_array == 0) || (alloc_index == alloc_count)) {
	    alloc_array = (struct __vpiSignal*)
		  calloc(alloc_count, sizeof(struct __vpiSignal));
	    alloc_index = 0;
      }

      struct __vpiSignal*cur = alloc_array + alloc_index;
      alloc_index += 1;
      return cur;
}

/*
 * Construct a vpiNet object. Give the object specified dimensions,
 * and point to the specified functor for the lsb.
 */
vpiHandle vpip_make_net(const char*name, int msb, int lsb,
			bool signed_flag, vvp_net_t*node)
{
      struct __vpiSignal*obj = allocate_vpiSignal();
      obj->base.vpi_type = &vpip_net_rt;
      obj->name = vpip_name_string(name);
      obj->msb = msb;
      obj->lsb = lsb;
      obj->signed_flag = signed_flag? 1 : 0;
      obj->isint_ = false;
      obj->node = node;

      obj->scope = vpip_peek_current_scope();

      count_vpi_nets += 1;

      return &obj->base;
}


/*
 * $Log: vpi_signal.cc,v $
 * Revision 1.74  2006/02/21 05:31:54  steve
 *  Put strings for reg objects.
 *
 * Revision 1.73  2006/02/21 02:39:27  steve
 *  Support string values for memory words.
 *
 * Revision 1.72  2005/11/30 00:42:14  steve
 *  vpi_signal supports vvp_fun_signal_vec types.
 *
 * Revision 1.71  2005/11/25 17:55:26  steve
 *  Put vec8 and vec4 nets into seperate net classes.
 *
 * Revision 1.70  2005/09/21 01:04:59  steve
 *  Support put_value of string values.
 *
 * Revision 1.69  2005/07/14 23:34:19  steve
 *  gcc4 compile errors.
 *
 * Revision 1.68  2005/06/13 00:54:04  steve
 *  More unified vec4 to hex string functions.
 *
 * Revision 1.67  2005/06/12 01:10:26  steve
 *  Remove useless references to functor.h
 *
 * Revision 1.66  2005/04/13 06:34:20  steve
 *  Add vvp driver functor for logic outputs,
 *  Add ostream output operators for debugging.
 *
 * Revision 1.65  2005/03/12 04:27:43  steve
 *  Implement VPI access to signal strengths,
 *  Fix resolution of ambiguous drive pairs,
 *  Fix spelling of scalar.
 *
 * Revision 1.64  2005/03/03 04:33:10  steve
 *  Rearrange how memories are supported as vvp_vector4 arrays.
 *
 * Revision 1.63  2004/12/11 02:31:30  steve
 *  Rework of internals to carry vectors through nexus instead
 *  of single bits. Make the ivl, tgt-vvp and vvp initial changes
 *  down this path.
 *
 * Revision 1.62  2004/05/19 03:26:25  steve
 *  Support delayed/non-blocking assignment to reals and others.
 *
 * Revision 1.61  2004/03/09 03:11:02  steve
 *  Get vpiModule of signals.
 *
 * Revision 1.60  2004/02/20 01:52:25  steve
 *  vpiStringVal does not include leading nulls.
 *
 * Revision 1.59  2004/02/19 21:31:59  steve
 *  vpiStringVal writes need to set all the bits of a reg.
 *
 * Revision 1.58  2003/08/15 18:23:56  steve
 *  vpiIntVal treats x and z bits as 0.
 *
 * Revision 1.57  2003/06/04 01:56:20  steve
 * 1) Adds configure logic to clean up compiler warnings
 * 2) adds acc_compare_handle, acc_fetch_range, acc_next_scope and
 *    tf_isetrealdelay, acc_handle_scope
 * 3) makes acc_next reentrant
 * 4) adds basic vpiWire type support
 * 5) fills in some acc_object_of_type() and acc_fetch_{full}type()
 * 6) add vpiLeftRange/RigthRange to signals
 *
 * Revision 1.56  2003/05/02 04:29:57  steve
 *  Add put_value with transport delay.
 *
 * Revision 1.55  2003/04/12 18:56:57  steve
 *  Add vpoiScalarVal support for signals.
 *
 * Revision 1.54  2003/03/06 04:32:00  steve
 *  Use hashed name strings for identifiers.
 *
 * Revision 1.53  2003/02/16 23:40:05  steve
 *  Permanent allocate vpiSignals more efficiently.
 *
 * Revision 1.52  2003/02/09 23:33:26  steve
 *  Spelling fixes.
 *
 * Revision 1.51  2003/01/07 18:07:50  steve
 *  Allocate res-buf in bigger chunks
 *
 * Revision 1.50  2002/11/25 23:33:45  steve
 *  Support put of vpiStringVal to signals.
 *
 * Revision 1.49  2002/09/11 16:06:57  steve
 *  Fix wrecked rbuf in vpi_get_str of signals and memories.
 *
 * Revision 1.48  2002/09/10 02:27:11  steve
 *  Actually set strength pointer when getting strength val.
 *
 * Revision 1.47  2002/09/06 04:56:29  steve
 *  Add support for %v is the display system task.
 *  Change the encoding of H and L outputs from
 *  the bufif devices so that they are logic x.
 *
 * Revision 1.46  2002/08/12 01:35:09  steve
 *  conditional ident string using autoconfig.
 *
 * Revision 1.45  2002/07/23 15:11:41  steve
 *  integral type/ptrdiff_t warning.
 */
