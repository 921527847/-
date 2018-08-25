#pragma once
#include <new>
#include <iostream>
using namespace std;

/**********************************************************************/
//一级空间配置器的实现
#define THROW_BAD_ALLOC cerr<<"out of memory"<<endl;exit(1)

template <int inst>
class Alloc_malloc
{
public:
	//Allocate用于申请空间
	static void* Allocate(size_t n)
	{
		void* result = malloc(n);
		//申请失败，使用oom_malloc()重新尝试申请
		if (result == NULL)
			result = Oom_malloc(n);
		return result;
	}

	//Deallocate用于释放空间
	static void Deallocate(void* p, size_t /* size*/)
	{
		free(p);
	}

	//Reallocate用于根据需要调整已经存在的空间的大小
	static void* Reallocate(void* p, size_t size)
	{
		void* result = realloc(p, size);
		//申请失败，使用oom_realloc()尝试申请
		if (result == NULL)
			result = Oom_realloc(p, size);
		return result;
	}

	//Set_malloc_handler函数是用于设置用户提供的释放空间的函数指针
	static void(*Set_malloc_handler(void(*f)())) ()
	{
		void(*old)() = _Malloc_alloc_oom_handler;
		_Malloc_alloc_oom_handler = f;
		return (old);
	}

private:
	//通过用户提供的释放空间（释放自己已经不用了的空间）的函数不断的释放空间并检测
	//直到释放出的空间足够分配给申请的空间
	//如果用户没有提供释放空间的函数，则抛异常
	static void* Oom_malloc(size_t size)
	{
		void* result;
		void(*My_malloc_handler)();
		for (;;)
		{
			My_malloc_handler = _Malloc_alloc_oom_handler;
			if (0 == My_malloc_handler)//用户没有提供释放空间的函数
				THROW_BAD_ALLOC;
			_Malloc_alloc_oom_handler();
			result = malloc(size);
			if (result)
				return result;
		}
	}

	static void* Oom_realloc(void* p, size_t size)
	{
		void* result;
		void(*my_malloc_handler) ();
		for (;;)
		{
			my_malloc_handler = _Malloc_alloc_oom_handler;
			if (0 == my_malloc_handler)
				THROW_BAD_ALLOC;
			_Malloc_alloc_oom_handler();
			result = realloc(p, size);
			if (result)
				return result;
		}
	}

private:
	static void(*_Malloc_alloc_oom_handler)();
};

//类外初始化静态成员变量 _malloc_alloc_oom_handler
template <int inst>
void(*Alloc_malloc<inst>::_Malloc_alloc_oom_handler)() = 0;

/***********************二级空间配置器的实现******************************/

enum{ ALINE = 8 }; //每次分配的最小空间的大小
enum { MAX_BYTES = 128 }; //二级空间配置器最大分配的空间大小
enum { NFREELISTS = MAX_BYTES / ALINE }; //free_lists的个数

template <int inst>
class default_Alloc
{
public:
	static void* Allocate(size_t size)//开辟空间
	{
		obj* volatile result;
		//大空间，使用一级空间配置器分配内存
		printf("申请%d字节的空间\n", size);
		if (size > MAX_BYTES)
		{
			printf("申请空间大于128字节，使用一级空间配置器\n");
			return Alloc_malloc<inst>::Allocate(size);
		}


		//小空间，使用二级空间配置器分配内存
		//先去对应的自由链表中找
		int index = Free_list_index(size);
		result = _free_list[index];
		if (NULL == result)//自由链表中没有空间，从内存池里申请空间
		{
			printf("第%d号自由链表中没有空间，向内存池中申请空间\n", index);
			result = (obj*)Refill(size);
			return result;
		}
		_free_list[index] = result->free_list_link;
		printf("在第%d号自由链表中获取到%d个字节的空间\n", index, size);
		return result;
	}

	static void Deallocate(void* p, size_t n)//释放空间
	{
		if (n > 128)
		{
			printf("释放%d个字节的空间，归还给系统\n", n);
			Alloc_malloc<inst>::Deallocate(p, n);
			return;
		}
		obj* pCur = (obj*)p;
		int index = Free_list_index(n);
		pCur->free_list_link = _free_list[index];
		_free_list[index] = pCur;
		printf("释放%d个字节的空间，挂接到%d号自由链表中\n\n", n, index);
	}

private:
	static size_t Round_up(size_t bytes)//用于上调至ALINE的整数倍
	{
		return (((bytes)+ALINE - 1)&~(ALINE - 1));
	}

	static size_t Free_list_index(size_t bytes)//用于计算在free_lists中的下标位置
	{
		return (bytes - 1) / ALINE;
		//return (bytes+(ALINE-1))/ALINE-1;  //SGI中的处理方式
	}
	static void* Refill(size_t bytes) //向内存池中申请空间
	{
		int nobjs = 20;
		//调用chunk_alloc函数，尝试获取nobjs个内存块
		char* chunk = Chunk_alloc(bytes, nobjs);
		obj* result;
		if (1 == nobjs)//只申请到一块空间的大小，将该块空间返回给用户使用
		{
			printf("从内存池里成功申请到%d个%d字节大小的内存块\n", nobjs, bytes);
			return chunk;
		}

		//否则代表有多块，将其中一块返回给用，其他的挂接到自由链表中
		printf("从内存池里成功申请到%d个%d字节大小的内存块\n", nobjs, bytes);
		result = (obj*)chunk;
		int n = Free_list_index(bytes);
		obj* next = (obj*)(chunk + bytes);
		while (1)
		{
			next->free_list_link = _free_list[n];
			_free_list[n];
			next = (obj*)((char*)next + bytes);
			if ((char*)next == chunk + nobjs*bytes)
			{
				printf("成功将剩下的%d个%d字节大小的内存块挂接到%d号自由链表中\n", nobjs - 1, bytes, n);
				break;
			}

		}
		return result;
	}


private:
	static char* Chunk_alloc(size_t size, int& nobjs)
	{
		char* result;
		size_t total_bytes = size*nobjs;
		size_t bytes_left = _end - _start;
		if (bytes_left >= total_bytes)//内存池里还有足够的空间
		{
			printf("向内存池申请%d个%d字节大小的内存块\n", nobjs, size);
			result = _start;
			_start += total_bytes;
			return result;
		}
		else if (bytes_left >= size)//内存池里边还能提供至少一块内存块
		{
			nobjs = bytes_left / size;
			result = _start;
			_start += nobjs*size;
			printf("向内存池申请%d个%d字节大小的内存块\n", nobjs, size);
			return result;
		}
		else
		{
			//内存池里边连一个内存块空间的大小都没有，朝系统里边申请空间填充内存池
			if (bytes_left > 0)//如果内存池里还剩有空间
			{
				int n = Free_list_index(bytes_left);
				((obj*)_start)->free_list_link = _free_list[n];
				_free_list[n] = (obj*)_start;
			}
			size_t bytetoget = 2 * total_bytes + Round_up(_heap_size >> 4);
			_start = (char*)malloc(bytetoget);
			printf("内存池空间不足%d字节，向系统申请%d字节大小的内存块\n", size, bytetoget);
			if (NULL == _start)//系统里边没有足够的空间
			{
				printf("系统空间不足，不能分配%d字节大小的内存块\n", bytetoget);
				//从当前自由链表开始遍历管理自由链表的数组
				for (size_t i = size; i <= MAX_BYTES; i++)
				{
					int n = Free_list_index(i);
					if (_free_list[n])//该自由链表里管理的有空间
					{
						_start = (char*)_free_list[n];
						_end = _start + i;
						printf("在第%d号自由链表中获取到%d个字节的空间放入内存池中\n", n, size);
						//递归调用自己，方便调整nobjs
						return (Chunk_alloc(size, nobjs));
					}
				}
				//已经是山穷水尽了，尝试调用一级空间配置器来申请
				printf("已经山穷水尽了，使用一级空间配置器来再次尝试申请空间\n");
				_end = 0;
				_start = (char*)Alloc_malloc<inst>::Allocate(size);
			}
			_end = _start + bytetoget;
			_heap_size += bytetoget;
			//递归调用自己，方便调整nobjs
			return (Chunk_alloc(size, nobjs));
		}
	}

private:
	union obj
	{
		union obj* free_list_link;
		char client_data[1];
	};

private:
	static char* _start;
	static char* _end;
	static size_t _heap_size;

	static obj* volatile _free_list[NFREELISTS];
};

//类外初始化类的静态成员变量
template <int inst>
char* default_Alloc<inst> ::_start = 0;

template <int inst>
char* default_Alloc<inst> ::_end = 0;

template <int inst>
size_t default_Alloc<inst> ::_heap_size = 0;

template <int inst>
typename default_Alloc<inst>::obj* volatile default_Alloc<inst>::_free_list[NFREELISTS] =
{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };


/*****************************测试部分*********************************/
//测试一级空间配置器
void test1()
{
	int* p = (int*)Alloc_malloc<0>::Allocate(3 * sizeof(int));
	p[0] = 0;
	p[1] = 1;
	p[2] = 2;
	p = (int*)Alloc_malloc<0>::Reallocate(p, 5 * sizeof(int));
	p[3] = 3;
	p[4] = 4;

	for (int i = 0; i<5; i++)
	{
		cout << p[i] << " ";
	}
	cout << endl;
}

void test2()
{
	int* p1 = (int*)default_Alloc<0>::Allocate(8);
	default_Alloc<0>::Deallocate(p1, 8);

	int* p = (int*)default_Alloc<0>::Allocate(8);
	default_Alloc<0>::Deallocate(p, 8);

	int* p2 = (int*)default_Alloc<0>::Allocate(20);
	default_Alloc<0>::Deallocate(p2, 20);

	int* p3 = (int*)default_Alloc<0>::Allocate(200);
	default_Alloc<0>::Deallocate(p1, 200);
}

int main()
{
	test1();
	test2();
	return 0;
}
/*我们知道，引入相对的复杂的空间配置器，主要源自两点：

1. 频繁使用malloc，free开辟释放小块内存带来的性能效率的低下
2. 内存碎片问题，导致不连续内存不可用的浪费

引入两层配置器帮我们解决以上的问题，但是也带来一些问题：

1:内碎片的问题，自由链表所挂区块都是8的整数倍，因此当我们需要非8倍数的区块，
往往会导致浪费，比如我只要1字节的大小，但是自由链表最低分配8块，也就是浪费了7字节，
我以为这也就是通常的以空间换时间的做法，这一点在计算机科学中很常见。
2:我们发现似乎没有释放自由链表所挂区块的函数？
确实是的，由于配置器的所有方法，成员都是静态的，那么他们就是存放在静态区。
释放时机就是程序结束，这样子会导致自由链表一直占用内存，自己进程可以用，其他进程却用不了。
1、在空间配置器中所有的函数和变量都是静态的，所以他们在程序结束的时候才会被释放发。二级空间配置器中没有将申请的内存还给操作系统，
只是将他们挂在自由链表上。所以说只有当你的程序结束了之后才会将开辟的内存还给操作系统。

2、由于它没有将内存还给操作系统，所以就会出现二种极端的情况。
2.1、假如我不断的开辟小块内存，最后将整个heap上的内存都挂在了自由链表上，但是都没有用这些空间，再想要开辟一个大块内存的话会开辟失败。
2.2、再比如我不断的开辟char,最后将整个heap内存全挂在了自由链表的第一个结点的后面，这时候我再想开辟一个16个字节的内存，也会失败。
或许我比较杞人忧天吧，总的来说上面的情况只是小概率情况。如果非得想办法解决的话，我想的是：针对2.1我们可以引入释放二级空间配置器的方法，
但是这个释放比较麻烦。针对2.2我们可以合并自由链表上的连续的小的内存块。*/