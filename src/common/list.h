/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2009-2012, 2015-2016  Université de Bordeaux
 * Copyright (C) 2010, 2011  CNRS
 *
 * StarPU is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 *
 * StarPU is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU Lesser General Public License in COPYING.LGPL for more details.
 */

/** @file
 * @brief Listes doublement chainées automatiques
 */


/** @remarks list how-to
 * *********************************************************
 * LIST_TYPE(FOO, contenu);
 *  - déclare les types suivants
 *      + pour les cellules : struct FOO
 *      + pour les listes : struct FOO_list
 *      + pour les itérateurs : struct FOO
 *  - déclare les accesseurs suivants :
 *     * création d'une cellule
 *   struct FOO*	FOO_new(void);
 *     * suppression d'une cellule
 *   void		FOO_delete(struct FOO*);
 *     * création d'une liste (vide)
 *   struct FOO_list*	FOO_list_new(void);
 *     * initialisation d'une liste (vide)
 *   void		FOO_list_init(struct FOO_list*);
 *     * suppression d'une liste
 *   void		FOO_list_delete(struct FOO_list*);
 *     * teste si une liste est vide
 *   int		FOO_list_empty(struct FOO_list*);
 *     * retire un élément de la liste
 *   void		FOO_list_erase(struct FOO_list*, struct FOO*);
 *     * ajoute une élément en queue de liste
 *   void		FOO_list_push_back(struct FOO_list*, struct FOO*);
 *     * ajoute un élément en tête de liste
 *   void		FOO_list_push_front(struct FOO_list*, struct FOO*);
 *     * ajoute un élément avant un élément
 *   void		FOO_list_insert_before(struct FOO_list*, struct FOO*new, struct FOO*);
 *     * ajoute un élément après un élément
 *   void		FOO_list_insert_after(struct FOO_list*, struct FOO*new, struct FOO*);
 *     * ajoute la deuxième liste à la fin de la première liste
 *   struct FOO*	FOO_list_push_list_back(struct FOO_list*, struct FOO_list*);
 *     * ajoute la première liste au début de la deuxième liste
 *   struct FOO*	FOO_list_push_list_front(struct FOO_list*, struct FOO_list*);
 *     * retire l'élément en queue de liste
 *   struct FOO*	FOO_list_pop_back(struct FOO_list*);
 *     * retire l'élement en tête de liste
 *   struct FOO*	FOO_list_pop_front(struct FOO_list*);
 *     * retourne l'élément en queue de liste
 *   struct FOO*	FOO_list_back(struct FOO_list*);
 *     * retourne l'élement en tête de liste
 *   struct FOO*	FOO_list_front(struct FOO_list*);
 *     * vérifie si la liste chainée est cohérente
 *   int		FOO_list_check(struct FOO_list*);
 *     * retourne le premier élément de la liste
 *   struct FOO*	FOO_list_begin(struct FOO_list*);
 *     * retourne la valeur à tester en fin de liste
 *   struct FOO*	FOO_list_end(struct FOO_list*);
 *     * retourne l'élément suivant de la liste
 *   struct FOO*	FOO_list_next(struct FOO*)
 *     * retourne la taille de la liste
 *   int		FOO_list_size(struct FOO_list*)
 *     * retourne la position de l'élément dans la liste (indexé à partir de 0)
 *   int		FOO_list_member(struct FOO_list*, struct FOO*)
 *     * teste si l'élément est dans la liste
 *   int		FOO_list_ismember(struct FOO_list*, struct FOO*)
 * *********************************************************
 * Exemples d'utilisation :
 *  - au départ, on a :
 *    struct ma_structure_s
 *    {
 *      int a;
 *      int b;
 *    };
 *  - on veut en faire une liste. On remplace la déclaration par :
 *    LIST_TYPE(ma_structure,
 *      int a;
 *      int b;
 *    );
 *    qui crée les types struct ma_structure et struct ma_structure_list.
 *  - allocation d'une liste vide :
 *  struct ma_structure_list * l = ma_structure_list_new();
 *  - ajouter un élément 'e' en tête de la liste 'l' :
 *  struct ma_structure * e = ma_structure_new();
 *  e->a = 0;
 *  e->b = 0;
 *  ma_structure_list_push_front(l, e);
 *  - itérateur de liste :
 *  struct ma_structure * i;
 *  for(i  = ma_structure_list_begin(l);
 *      i != ma_structure_list_end(l);
 *      i  = ma_structure_list_next(i))
 *  {
 *    printf("a=%d; b=%d\n", i->a, i->b);
 *  }
 * *********************************************************
 */



/**@hideinitializer
 * Generates a new type for list of elements */
#define LIST_TYPE(ENAME, DECL) \
  LIST_CREATE_TYPE(ENAME, DECL)

/**@hideinitializer
 * The effective type declaration for lists */
#define LIST_CREATE_TYPE(ENAME, DECL) \
  /** from automatic type: struct ENAME */ \
  struct ENAME \
  { \
    struct ENAME *_prev; /**< @internal previous cell */ \
    struct ENAME *_next; /**< @internal next cell */ \
    DECL \
  }; \
  /** @internal */ \
  struct ENAME##_list \
  { \
    struct ENAME *_head; /**< @internal head of the list */ \
    struct ENAME *_tail; /**< @internal tail of the list */ \
  }; \
  /** @internal */static inline struct ENAME *ENAME##_new(void) \
    { struct ENAME *e = (struct ENAME *)malloc(sizeof(struct ENAME)); \
      e->_next = NULL; e->_prev = NULL; return e; } \
  /** @internal */static inline void ENAME##_delete(struct ENAME *e) \
    { free(e); } \
  /** @internal */static inline void ENAME##_list_push_front(struct ENAME##_list *l, struct ENAME *e) \
    { if(l->_tail == NULL) l->_tail = e; else l->_head->_prev = e; \
      e->_prev = NULL; e->_next = l->_head; l->_head = e; } \
  /** @internal */static inline void ENAME##_list_push_back(struct ENAME##_list *l, struct ENAME *e) \
    { if(l->_head == NULL) l->_head = e; else l->_tail->_next = e; \
      e->_next = NULL; e->_prev = l->_tail; l->_tail = e; } \
  /** @internal */static inline void ENAME##_list_insert_before(struct ENAME##_list *l, struct ENAME *e, struct ENAME *o) \
    { struct ENAME *p = o->_prev; if (p) { p->_next = e; e->_prev = p; } else { l->_head = e; e->_prev = NULL; } \
      e->_next = o; o->_prev = e; } \
  /** @internal */static inline void ENAME##_list_insert_after(struct ENAME##_list *l, struct ENAME *e, struct ENAME *o) \
    { struct ENAME *n = o->_next; if (n) { n->_prev = e; e->_next = n; } else { l->_tail = e; e->_next = NULL; } \
      e->_prev = o; o->_next = e; } \
  /** @internal */static inline void ENAME##_list_push_list_front(struct ENAME##_list *l1, struct ENAME##_list *l2) \
    { if (l2->_head == NULL) { l2->_head = l1->_head; l2->_tail = l1->_tail; } \
      else if (l1->_head != NULL) { l1->_tail->_next = l2->_head; l2->_head->_prev = l1->_tail; l2->_head = l1->_head; } } \
  /** @internal */static inline void ENAME##_list_push_list_back(struct ENAME##_list *l1, struct ENAME##_list *l2) \
    { if(l1->_head == NULL) { l1->_head = l2->_head; l1->_tail = l2->_tail; } \
      else if (l2->_head != NULL) { l1->_tail->_next = l2->_head; l2->_head->_prev = l1->_tail; l1->_tail = l2->_tail; } } \
  /** @internal */static inline struct ENAME *ENAME##_list_front(const struct ENAME##_list *l) \
    { return l->_head; } \
  /** @internal */static inline struct ENAME *ENAME##_list_back(const struct ENAME##_list *l) \
    { return l->_tail; } \
  /** @internal */static inline void ENAME##_list_init(struct ENAME##_list *l) \
    { l->_head=NULL; l->_tail=l->_head; } \
  /** @internal */static inline struct ENAME##_list *ENAME##_list_new(void) \
    { struct ENAME##_list *l; l=(struct ENAME##_list *)malloc(sizeof(struct ENAME##_list)); \
      ENAME##_list_init(l); return l; } \
  /** @internal */static inline int ENAME##_list_empty(const struct ENAME##_list *l) \
    { return (l->_head == NULL); } \
  /** @internal */static inline void ENAME##_list_delete(struct ENAME##_list *l) \
    { free(l); } \
  /** @internal */static inline void ENAME##_list_erase(struct ENAME##_list *l, struct ENAME *c) \
    { struct ENAME *p = c->_prev; if(p) p->_next = c->_next; else l->_head = c->_next; \
      if(c->_next) c->_next->_prev = p; else l->_tail = p; } \
  /** @internal */static inline struct ENAME *ENAME##_list_pop_front(struct ENAME##_list *l) \
    { struct ENAME *e = ENAME##_list_front(l); \
      ENAME##_list_erase(l, e); return e; } \
  /** @internal */static inline struct ENAME *ENAME##_list_pop_back(struct ENAME##_list *l) \
    { struct ENAME *e = ENAME##_list_back(l); \
      ENAME##_list_erase(l, e); return e; } \
  /** @internal */static inline struct ENAME *ENAME##_list_begin(const struct ENAME##_list *l) \
    { return l->_head; } \
  /** @internal */static inline struct ENAME *ENAME##_list_end(const struct ENAME##_list *l STARPU_ATTRIBUTE_UNUSED) \
    { return NULL; } \
  /** @internal */static inline struct ENAME *ENAME##_list_next(const struct ENAME *i) \
    { return i->_next; } \
  /** @internal */static inline int ENAME##_list_ismember(const struct ENAME##_list *l, const struct ENAME *e) \
    { struct ENAME *i=l->_head; while(i!=NULL){ if (i == e) return 1; i=i->_next; } return 0; } \
  /** @internal */static inline int ENAME##_list_member(const struct ENAME##_list *l, const struct ENAME *e) \
    { struct ENAME *i=l->_head; int k=0; while(i!=NULL){if (i == e) return k; k++; i=i->_next; } return -1; } \
  /** @internal */static inline int ENAME##_list_size(const struct ENAME##_list *l) \
    { struct ENAME *i=l->_head; int k=0; while(i!=NULL){k++;i=i->_next;} return k; } \
  /** @internal */static inline int ENAME##_list_check(const struct ENAME##_list *l) \
    { struct ENAME *i=l->_head; while(i) \
    { if ((i->_next == NULL) && i != l->_tail) return 0; \
      if (i->_next == i) return 0; \
      i=i->_next;} return 1; }
