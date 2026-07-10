#include "temperature.h"



u16 correct_table[TAU_TABLE_SIZE]={0};
EnvParam_t org_env_param = { 0,0,0,0 };             //origin environment parameter
EnvParam_t new_env_param = { 0,0,0,0 };             //new environment parameter
uint8_t gain_flag = 1;
EnvFactor_t org_env_factor = { 0 };                 //origin environment correction factor
EnvFactor_t new_env_factor = { 0 };                 //new environment correction factor
uint16_t nuc_table[NUCT_LEN] = { 0 };               //nuc-t table

int read_nuc_parameter(void)
{
	int i = 0;
	int j=0;
	uint8_t temp = 0;
	uint8_t data[0x100] = { 0 };
	for(i=0;i<sizeof(nuc_table)/sizeof(data);i++)
	{
		if(spi_read(0xda000+i*sizeof(data), sizeof(data), data)!=IR_SUCCESS)//high gain is 0xda000 / low gain is 0xd3000
		{
			printf("get nuc-t table failed\n");
			return FAIL;
		}
		//memcpy(nuc_table+i*sizeof(data),data,sizeof(data));
		for(j = 0; j<sizeof(data);)
		{
			nuc_table[(i*sizeof(data)+j)/2]=((uint16_t)data[j]<<8)+data[j+1];		
			j=j+2;
		}
	}
	printf("get nuc-t table successed\n");
	return SUCCESS;
}



int calculate_org_env_cali_parameter(void)
{
	uint8_t gain_flag = HIGH_GAIN;
	if(get_prop_tpd_params(TPD_PROP_EMS, (uint16_t*)&org_env_param.EMS) != IR_SUCCESS)
	{
		printf("get EMS failed\n");
		return FAIL;
	}
	if (get_prop_tpd_params(TPD_PROP_TAU, (uint16_t*)&org_env_param.TAU) != IR_SUCCESS)
	{
		printf("get TAU failed\n");
		return FAIL;
	}
	if (get_prop_tpd_params(TPD_PROP_TA, (uint16_t*)&org_env_param.Ta) != IR_SUCCESS)
	{
		printf("get TA failed\n");
		return FAIL;
	}
	if (get_prop_tpd_params(TPD_PROP_TU, (uint16_t*)&org_env_param.Tu) != IR_SUCCESS)
	{
		printf("get TU failed\n");
		return FAIL;
	}
	if (calculate_org_KE_and_BE_with_nuc_t(&org_env_param, nuc_table,gain_flag, &org_env_factor) != IRTEMP_SUCCESS)
	{
		printf("calculate_KE_and_BE failed\n");
		return FAIL;
	}
	printf("origin K_E=%d, origin B_E=%d\n",org_env_factor.K_E,org_env_factor.B_E);
	return SUCCESS;
}


int calculate_new_env_cali_parameter(uint16_t * correct_table, float ems, float ta, float tu, float dist, float hum)
{
	uint16_t tau = 0;
	if (read_tau(correct_table, hum, ta, dist, &tau) != IRTEMP_SUCCESS)
	{
		printf("read tau failed\n");
		return FAIL;
	}
	new_env_param.EMS = ems * (1 << 14);
	new_env_param.TAU = tau;
	new_env_param.Ta = (ta + 273) * (1 << 4);
	new_env_param.Tu = (tu + 273) * (1 << 4);
	if (calculate_new_KE_and_BE_with_nuc_t(&new_env_param, nuc_table, gain_flag, &new_env_factor) != IRTEMP_SUCCESS)
	{
		printf("calculate_KE_and_BE failed\n");
		return FAIL;
	}
	printf("new K_E=%d, new B_E=%d\n",new_env_factor.K_E,new_env_factor.B_E);
	return SUCCESS;
}


int temp_calc_with_new_env_calibration( float org_temp, float* new_temp)
{
	uint16_t nuc_cal = 0;
	uint16_t nuc_org = 0;
	irtemp_error_t ret;
	uint16_t temp_data = 0;

	ret = reverse_calc_NUC_with_nuc_t(nuc_table, org_temp-273.15, &nuc_cal);
	if (ret != IRTEMP_SUCCESS)
	{
		printf("reverse_calc_NUC_with_nuc_t failed\n");
		return FAIL;
	};
	//printf("nuc_cal=%d\n",nuc_cal);
	ret = reverse_calc_NUC_without_env_correct(&org_env_factor, nuc_cal, &nuc_org);
	if (ret != IRTEMP_SUCCESS)
	{
		printf("reverse_calc_NUC_without_env_correct failed\n");
		return FAIL;
	};
	//printf("nuc_org=%d\n",nuc_org);
	ret = recalc_NUC_with_env_correct(&new_env_factor, nuc_org, &nuc_cal);
	if (ret != IRTEMP_SUCCESS)
	{
		printf("recalc_NUC_with_env_correct failed\n");
		return FAIL;
	};
	//printf("nuc_cal=%d\n",nuc_cal);
	ret = remap_temp(nuc_table, nuc_cal, &temp_data);
	if (ret != IRTEMP_SUCCESS)
	{
		printf("remap_temp failed\n");
		return FAIL;
	};
	*new_temp = (float)temp_data / 16;
	return SUCCESS;
}
