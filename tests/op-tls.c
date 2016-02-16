__thread int tls_i32 = 1;
//__thread char tls_i8 = 1;
__thread long long tls_i64 = 1;

int f1(int i)
{
	return tls_i32 + tls_i64 + i;
}
