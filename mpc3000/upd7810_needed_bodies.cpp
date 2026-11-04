// MOV_MM_A
void MOV_MM_A()
{
	MM = A;

	if (BIT(A, 3))
		m_ram_view.select(0);
	else
		m_ram_view.disable();
}

// MVI_MKL_xx
void MVI_MKL_xx()
{
	RDOPARG( MKL );
}

// MVI_MKH_xx
void MVI_MKH_xx()
{
	RDOPARG( MKH );
}

// MOV_SMH_A
void MOV_SMH_A()
{
	write_smh(A);
}

// MOV_SML_A
void MOV_SML_A()
{
	SML = A;
}

// MOV_MA_A
void MOV_MA_A()
{
	if (MA == A)
		return;
	MA = A;
	WP(UPD7810_PORTA, m_pa_out);
}

// MOV_MB_A
void MOV_MB_A()
{
	if (MB == A)
		return;
	MB = A;
	WP(UPD7810_PORTB, m_pb_out);
}

// MOV_MCC_A
void MOV_MCC_A()
{
	if (MCC == A)
		return;
	MCC = A;
	WP(UPD7810_PORTC, m_pc_out);
}

// MOV_MC_A
void MOV_MC_A()
{
	if (MC == A)
		return;
	MC = A;
	WP(UPD7810_PORTC, m_pc_out);
}

// LXI_EA_s
void LXI_EA_s()
{
	RDOPARG( EAL );
	RDOPARG( EAH );
}

// illegal2
void illegal2()
{
	logerror("uPD7810 '%s': illegal opcode %02x %02x at PC:%04x\n", tag(), OP, OP2, PC);
}

// MOV_ETMM_A
void MOV_ETMM_A()
{
	ETMM = A;
}

// MVI_TMM_xx
void MVI_TMM_xx()
{
	RDOPARG( TMM );
}

// MVI_V_xx
void MVI_V_xx()
{
	RDOPARG( V );
}

// STAX_Hp
void STAX_Hp()
{
	WM( HL, A );
	HL++;
}

// DEQ_EA_HL
void DEQ_EA_HL()
{
	uint16_t tmp;

	tmp = EA - HL;
	ZHC_SUB( tmp, EA, 0 );
	SKIP_Z;
}

// SHLD_w
void SHLD_w()
{
	PAIR ea;
	ea.d = 0;

	RDOPARG( ea.b.l );
	RDOPARG( ea.b.h );
	WM( ea.d, L );
	WM( ea.d + 1, H );
}

// EI
void EI()
{
	m_iff_pending = 1;
}

// EQIW_wa_xx
void EQIW_wa_xx()
{
	PAIR ea = m_va;
	uint8_t tmp, m, imm;

	RDOPARG( ea.b.l );
	RDOPARG( imm );
	m = RM( ea.d );
	tmp = m - imm;
	ZHC_SUB( tmp, m, 0 );
	SKIP_Z;
}

// ADI_A_xx
void ADI_A_xx()
{
	uint8_t tmp, imm;

	RDOPARG( imm );
	tmp = A + imm;

	ZHC_ADD( tmp, A, 0 );
	A = tmp;
}

// ANI_A_xx
void ANI_A_xx()
{
	uint8_t imm;
	RDOPARG( imm );
	A &= imm;
	SET_Z(A);
}

// ADD_E_A
void ADD_E_A()
{
	uint8_t tmp = E + A;
	ZHC_ADD( tmp, E, 0 );
	E = tmp;
}

// ADD_L_A
void ADD_L_A()
{
	uint8_t tmp = L + A;
	ZHC_ADD( tmp, L, 0 );
	L = tmp;
}

// MOV_B_A
void MOV_B_A()
{
	B = A;
}

// ADD_A_A
void ADD_A_A()
{
	uint8_t tmp = A + A;
	ZHC_ADD( tmp, A, 0 );
	A = tmp;
}

// MOV_EAL_A
void MOV_EAL_A()
{
	EAL = A;
}

// PUSH_HL
void PUSH_HL()
{
	SP--;
	WM( SPD, H );
	SP--;
	WM( SPD, L );
}

// MOV_A_B
void MOV_A_B()
{
	A = B;
}

// SLL_A
void SLL_A()
{
	PSW = (PSW & ~CY) | ((A >> 7) & CY);
	A <<= 1;
}

// MOV_PC_A
void MOV_PC_A()
{
	WP( UPD7810_PORTC, A );
}

// POP_HL
void POP_HL()
{
	L = RM( SPD );
	SP++;
	H = RM( SPD );
	SP++;
}

// LDAX_H
void LDAX_H()
{
	A = RM( HL );
}

// MOV_C_A
void MOV_C_A()
{
	C = A;
}

// LDAX_D
void LDAX_D()
{
	A = RM( DE );
}

// MOV_A_PA
void MOV_A_PA()
{
	A = RP( UPD7810_PORTA );
}

// XRI_A_xx
void XRI_A_xx()
{
	uint8_t imm;
	RDOPARG( imm );
	A ^= imm;
	SET_Z(A);
}

// STAX_D
void STAX_D()
{
	WM( DE, A );
}

// ANA_A_B
void ANA_A_B()
{
	A &= B;
	SET_Z(A);
}

// STAX_H
void STAX_H()
{
	WM( HL, A );
}

// EQI_A_xx
void EQI_A_xx()
{
	uint8_t tmp, imm;

	RDOPARG( imm );
	tmp = A - imm;
	ZHC_SUB( tmp, A, 0 );
	SKIP_Z;
}

// NEI_A_xx
void NEI_A_xx()
{
	uint8_t tmp, imm;

	RDOPARG( imm );
	tmp = A - imm;
	ZHC_SUB( tmp, A, 0 );
	SKIP_NZ;
}

// SLR_A
void SLR_A()
{
	PSW = (PSW & ~CY) | (A & CY);
	A >>= 1;
}

// SLR_C
void SLR_C()
{
	PSW = (PSW & ~CY) | (C & CY);
	C >>= 1;
}

// INX_EA
void INX_EA()
{
	EA++;
}

// PUSH_VA
void PUSH_VA()
{
	SP--;
	WM( SPD, V );
	SP--;
	WM( SPD, A );
}

// PUSH_BC
void PUSH_BC()
{
	SP--;
	WM( SPD, B );
	SP--;
	WM( SPD, C );
}

// PUSH_EA
void PUSH_EA()
{
	SP--;
	WM( SPD, EAH );
	SP--;
	WM( SPD, EAL );
}

// ONI_C_xx
void ONI_C_xx()
{
	uint8_t imm;

	RDOPARG( imm );
	SET_Z( C & imm );
	SKIP_NZ;
}

// MOV_A_EAL
void MOV_A_EAL()
{
	A = EAL;
}

// LDAX_H_A
void LDAX_H_A()
{
	uint16_t ea;
	ea = HL + A;
	A = RM( ea );
}

// POP_EA
void POP_EA()
{
	EAL = RM( SPD );
	SP++;
	EAH = RM( SPD );
	SP++;
}

// POP_BC
void POP_BC()
{
	C = RM( SPD );
	SP++;
	B = RM( SPD );
	SP++;
}

// POP_VA
void POP_VA()
{
	A = RM( SPD );
	SP++;
	V = RM( SPD );
	SP++;
}

// BIT_4_wa
void BIT_4_wa()
{
	PAIR ea = m_va;

	RDOPARG( ea.b.l );

	if (RM( ea.d ) & 0x10)
		PSW |= SK;
}

// INR_C
void INR_C()
{
	uint8_t old_cy = PSW & CY;
	uint8_t tmp = C + 1;
	ZHC_ADD( tmp, C, 0 );
	C = tmp;
	SKIP_CY;
	PSW = ( PSW & ~CY ) | old_cy;
}

// DCR_A
void DCR_A()
{
	uint8_t old_cy = PSW & CY;
	uint8_t tmp = A - 1;
	ZHC_SUB( tmp, A, 0 );
	A = tmp;
	SKIP_CY;
	PSW = ( PSW & ~CY ) | old_cy;
}

// EXH
void EXH()
{
	uint16_t tmp;
	tmp = HL; HL = HL2; HL2 = tmp;
}

// INR_A
void INR_A()
{
	uint8_t old_cy = PSW & CY;
	uint8_t tmp = A + 1;
	ZHC_ADD( tmp, A, 0 );
	A = tmp;
	SKIP_CY;
	PSW = ( PSW & ~CY ) | old_cy;
}

// CALF
void CALF()
{
	PAIR w;
	w.d = 0;

	RDOPARG( w.b.l );
	w.b.h = 0x08 + (OP & 0x07);

	SP--;
	WM( SPD, PCH );
	SP--;
	WM( SPD, PCL );

	PCD = w.d;
}

// BIT_2_wa
void BIT_2_wa()
{
	PAIR ea = m_va;

	RDOPARG( ea.b.l );

	if (RM( ea.d ) & 0x04)
		PSW |= SK;
}

// BIT_3_wa
void BIT_3_wa()
{
	PAIR ea = m_va;

	RDOPARG( ea.b.l );

	if (RM( ea.d ) & 0x08)
		PSW |= SK;
}

// BIT_1_wa
void BIT_1_wa()
{
	PAIR ea = m_va;

	RDOPARG( ea.b.l );

	if (RM( ea.d ) & 0x02)
		PSW |= SK;
}

// ONI_A_xx
void ONI_A_xx()
{
	uint8_t imm;

	RDOPARG( imm );
	SET_Z( A & imm );
	SKIP_NZ;
}

// BIT_5_wa
void BIT_5_wa()
{
	PAIR ea = m_va;

	RDOPARG( ea.b.l );

	if (RM( ea.d ) & 0x20)
		PSW |= SK;
}

// ONI_PA_xx
void ONI_PA_xx()
{
	uint8_t pa = RP( UPD7810_PORTA ), imm;

	RDOPARG( imm );
	SET_Z( pa & imm );
	SKIP_NZ;
}

// MVI_L_xx
void MVI_L_xx()
{
	if (PSW & L0) { /* overlay active? */
		PC++;
		return;     /* NOP */
	}
	RDOPARG( L );
	PSW |= L0;
}

// DAA
void DAA()
{
	uint8_t l = A & 0x0f;
	uint8_t h = A >> 4;
	uint8_t adj = 0x00;
	uint8_t old_cy = PSW & CY;

	if (0 == (PSW & HC))
	{
		if (l < 10)
		{
			if (!(h < 10 && 0 == (PSW & CY)))
			adj = 0x60;
		}
		else
		{
			if (h < 9 && 0 == (PSW & CY))
				adj = 0x06;
			else
				adj = 0x66;
		}
	}
	else if (l < 3)
	{
		if (h < 10 && 0 == (PSW & CY))
			adj = 0x06;
		else
			adj = 0x66;
	}

	uint8_t tmp = A + adj;
	ZHC_ADD( tmp, A, PSW & CY );
	PSW |= old_cy;
	A = tmp;
}

// MVI_H_xx
void MVI_H_xx()
{
	RDOPARG( H );
}

// SUI_A_xx
void SUI_A_xx()
{
	uint8_t tmp, imm;

	RDOPARG( imm );
	tmp = A - imm;
	ZHC_SUB( tmp, A, 0 );
	A = tmp;
}

// MVIX_DE_xx
void MVIX_DE_xx()
{
	uint8_t imm;
	RDOPARG( imm );
	WM( DE, imm );
}

// SBI_E_xx
void SBI_E_xx()
{
	uint8_t tmp, imm;

	RDOPARG( imm );
	tmp = E - imm - (PSW & CY);
	ZHC_SUB( tmp, E, (PSW & CY) );
	E = tmp;
}

// SOFTI
void SOFTI()
{
	SP--;
	WM( SPD, PSW );
	SP--;
	WM( SPD, PCH );
	SP--;
	WM( SPD, PCL );

	PC = 0x0060;
}

// illegal
void illegal()
{
	logerror("uPD7810 '%s': illegal opcode %02x at PC:%04x\n", tag(), OP, PC);
}

// OFFI_PC_xx
void OFFI_PC_xx()
{
	uint8_t pc = RP( UPD7810_PORTC ), imm;

	RDOPARG( imm );
	SET_Z( pc & imm );
	SKIP_Z;
}

// ORI_A_xx
void ORI_A_xx()
{
	uint8_t imm;
	RDOPARG( imm );
	A |= imm;
	SET_Z(A);
}

// MOV_B_w
void MOV_B_w()
{
	PAIR ea;
	ea.d = 0;

	RDOPARG( ea.b.l );
	RDOPARG( ea.b.h );
	B = RM( ea.d );
}

// ORA_A_B
void ORA_A_B()
{
	A |= B;
	SET_Z(A);
}

// MOV_w_B
void MOV_w_B()
{
	PAIR ea;
	ea.d = 0;

	RDOPARG( ea.b.l );
	RDOPARG( ea.b.h );
	WM( ea.d, B );
}

// SLRC_C
void SLRC_C()
{
	PSW = (PSW & ~CY) | (C & CY);
	C >>= 1;
	SKIP_CY;
}

// MOV_A_C
void MOV_A_C()
{
	A = C;
}

// TABLE
void TABLE()
{
	uint16_t ea = PC + A + 1;
	C = RM( ea );
	B = RM( ea + 1 );
}

// MOV_A_EAH
void MOV_A_EAH()
{
	A = EAH;
}

// MOV_EAH_A
void MOV_EAH_A()
{
	EAH = A;
}

// DMOV_EA_HL
void DMOV_EA_HL()
{
	EA = HL;
}

// DNE_EA_HL
void DNE_EA_HL()
{
	uint16_t tmp;

	tmp = EA - HL;
	ZHC_SUB( tmp, EA, 0 );
	SKIP_NZ;
}

// LDEAX_Hp
void LDEAX_Hp()
{
	EAL = RM( HL );
	EAH = RM( HL + 1 );
	HL += 2;
}

// ANI_L_xx
void ANI_L_xx()
{
	uint8_t imm;
	RDOPARG( imm );
	L &= imm;
	SET_Z(L);
}

// ORI_L_xx
void ORI_L_xx()
{
	uint8_t imm;
	RDOPARG( imm );
	L |= imm;
	SET_Z(L);
}

// DMOV_BC_EA
void DMOV_BC_EA()
{
	BC = EA;
}

// ONI_B_xx
void ONI_B_xx()
{
	uint8_t imm;

	RDOPARG( imm );
	SET_Z( B & imm );
	SKIP_NZ;
}

// OFFI_B_xx
void OFFI_B_xx()
{
	uint8_t imm;

	RDOPARG( imm );
	SET_Z( B & imm );
	SKIP_Z;
}

// ANI_B_xx
void ANI_B_xx()
{
	uint8_t imm;
	RDOPARG( imm );
	B &= imm;
	SET_Z(B);
}

// SKIT_FST
void SKIT_FST()
{
	if (IRR & INTFST)
		PSW |= SK;
	IRR &= ~INTFST;
}

// MOV_TXB_A
void MOV_TXB_A()
{
	TXB = A;
	upd7810_write_TXB();
}

// EXX
void EXX()
{
	uint16_t tmp;
	tmp = BC; BC = BC2; BC2 = tmp;
	tmp = DE; DE = DE2; DE2 = tmp;
	tmp = HL; HL = HL2; HL2 = tmp;
}

// ORI_TMM_xx
void ORI_TMM_xx()
{
	uint8_t imm;

	RDOPARG( imm );
	TMM |= imm;
	SET_Z(TMM);
}

// SKIT_FT0
void SKIT_FT0()
{
	if (IRR & INTFT0)
		PSW |= SK;
	IRR &= ~INTFT0;
}

// MOV_A_PB
void MOV_A_PB()
{
	A = RP( UPD7810_PORTB );
}

// MOV_C_w
void MOV_C_w()
{
	PAIR ea;
	ea.d = 0;

	RDOPARG( ea.b.l );
	RDOPARG( ea.b.h );
	C = RM( ea.d );
}

// ANA_C_A
void ANA_C_A()
{
	C &= A;
	SET_Z(C);
}

// ORAW_wa
void ORAW_wa()
{
	PAIR ea = m_va;
	RDOPARG( ea.b.l );

	A |= RM( ea.d );
	SET_Z(A);
}
